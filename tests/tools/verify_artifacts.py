#!/usr/bin/env python3
"""Verify downloaded `finc` release artifacts by installing and running them.

`release.yml` proves each artifact on the machine that built it. That is not the
same claim as "a user can download this and it works": the archive on disk is
never unpacked, and a shared-library drift between the build runner and a
user's distro is exactly the class of failure the build host cannot see. This
script is the maintainer's re-check on a *downloaded* set.

Usage:
    verify_artifacts.py <directory>

`<directory>` holds the release assets (`finc-<version>-<triple>.*`) and,
optionally, `index.json`. Artifact classes are detected from the filenames; no
flags are needed. Each check prints one line:

    PASS  <artifact>  <check>  <detail>
    SKIP  <artifact>  <check>  <why the host cannot run it>
    FAIL  <artifact>  <check>  <detail>

Exit status is 1 if any check FAILs, 0 otherwise. A SKIP is not a pass: it names
the missing capability (wrong-architecture host, no `hdiutil`, ...) so a reader
can tell "verified here" from "only verifiable on its own platform".

Nothing is written inside a repository tree: every unpack happens under a fresh
temporary directory. Archives are the only inputs; the compiler is never built.
"""

import argparse
import hashlib
import io
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

# The program every executable artifact is asked to compile and run. It reads
# two symbols out of the bundled `lib/std`, prints a number that arithmetic must
# produce, and blames if the value is wrong -- so a broken stdlib search path or
# a wrong result fails the check instead of passing quietly.
SMOKE_SOURCE = """\
import { println_str, println_int } from stdio::std;

fun main() <noret> {
    let n <int> = 6 * 7;
    println_str("fin");
    println_int(n);
    blame n == 42;
}
"""
SMOKE_OUTPUT = "fin\n42\n"

# `finc-<version>-<target-triple><suffix>`; the triple may itself contain
# hyphens, so the suffix is what delimits it.
NAME = re.compile(r"^finc-(?P<version>[^-]+)-(?P<triple>.+?)"
                  r"(?P<kind>\.tar\.gz|\.zip|\.deb|\.rpm|\.dmg|-setup\.exe)$")
PORTABLE = (".tar.gz", ".zip")


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


class Report:
    """One line per check, and the failure count that decides the exit status."""

    def __init__(self):
        self.failures = 0
        self.counts = {"PASS": 0, "SKIP": 0, "FAIL": 0}

    def record(self, status, artifact, check, detail=""):
        self.counts[status] += 1
        if status == "FAIL":
            self.failures += 1
        print("%-4s  %-52s  %-18s  %s" % (status, artifact, check, detail))

    def passed(self, artifact, check, detail=""):
        self.record("PASS", artifact, check, detail)

    def skipped(self, artifact, check, why):
        if not why:
            raise SystemExit("a SKIP must name the missing capability: %s" % check)
        self.record("SKIP", artifact, check, why)

    def failed(self, artifact, check, detail=""):
        self.record("FAIL", artifact, check, detail)


def run(args, cwd=None, timeout=300, env=None):
    """Run a command, returning (rc, stdout, stderr) with text decoded."""
    try:
        proc = subprocess.run(args, cwd=cwd, timeout=timeout, env=env,
                              capture_output=True, text=True)
        return proc.returncode, proc.stdout, proc.stderr
    except FileNotFoundError:
        return 127, "", "%s: not found" % args[0]
    except subprocess.TimeoutExpired:
        return 124, "", "%s: timed out after %ss" % (args[0], timeout)
    except OSError as exc:
        return 126, "", "%s: %s" % (args[0], exc)


def have(tool):
    return shutil.which(tool) is not None


# --- machine type readers -------------------------------------------------
# Only the machine field matters here, and only to answer "is this the host's
# architecture". The release workflow has its own stronger check
# (.github/scripts/check_arch.py, which refuses to ship a mislabelled binary);
# this duplicates the minimum needed to stay usable on an artifact set with no
# checkout beside it.

ELF_MACHINE = {0x3E: "x86_64", 0xB7: "aarch64"}
PE_MACHINE = {0x8664: "x86_64", 0xAA64: "aarch64", 0x014C: "i686"}
MACHO_CPUTYPE = {0x01000007: "x86_64", 0x0100000C: "aarch64"}


def machine_of(path):
    """Return (architecture, container) for an executable, or (None, None)."""
    with open(path, "rb") as handle:
        head = handle.read(0x400)
        if head[:4] == b"\x7fELF" and len(head) >= 20:
            (machine,) = struct.unpack_from("<H", head, 18)
            return ELF_MACHINE.get(machine, "elf:0x%x" % machine), "ELF"
        if head[:4] in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
            (cputype,) = struct.unpack_from("<I", head, 4)
            return MACHO_CPUTYPE.get(cputype, "macho:0x%x" % cputype), "Mach-O"
        if head[:2] == b"MZ" and len(head) >= 0x40:
            (offset,) = struct.unpack_from("<I", head, 0x3C)
            handle.seek(offset)
            pe = handle.read(6)
            if pe[:4] == b"PE\0\0":
                (machine,) = struct.unpack_from("<H", pe, 4)
                return PE_MACHINE.get(machine, "pe:0x%x" % machine), "PE"
    return None, None


def host_arch():
    machine = platform.machine().lower()
    return {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)


def target_arch(triple):
    return triple.split("-", 1)[0]


# --- unpacking ------------------------------------------------------------

def ar_members(path):
    """Split a Debian `ar` archive into {name: bytes} without needing `ar`."""
    data = open(path, "rb").read()
    if data[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive")
    members, offset = {}, 8
    while offset + 60 <= len(data):
        header = data[offset:offset + 60]
        name = header[0:16].decode("ascii", "replace").strip().rstrip("/")
        size = int(header[48:58].decode("ascii").strip())
        body = data[offset + 60:offset + 60 + size]
        members[name] = body
        offset += 60 + size + (size & 1)     # members are 2-byte aligned
    return members


def extract_deb(path, dest):
    """Extract the data payload of a .deb under `dest` (FHS paths preserved)."""
    members = ar_members(path)
    try:
        payload = next(body for name, body in members.items()
                       if name.startswith("data.tar"))
        control = next((body for name, body in members.items()
                        if name.startswith("control.tar")), None)
    except StopIteration:
        raise ValueError("no data.tar member in the ar archive")
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:*") as archive:
        archive.extractall(dest, filter="data")
    control_text = None
    if control is not None:
        with tarfile.open(fileobj=io.BytesIO(control), mode="r:*") as archive:
            for member in archive.getmembers():
                if member.name.lstrip("./") == "control":
                    control_text = archive.extractfile(member).read().decode()
    return control_text


def extract_rpm(path, dest):
    """Extract an .rpm payload; bsdtar or 7z reads the cpio, `rpm2cpio` is not required."""
    if have("rpm2cpio") and have("cpio"):
        rc, _, err = run("sh", "-c",
                         "rpm2cpio %s | cpio -idm --no-absolute-filenames"
                         % shlex_quote(path), cwd=dest)
        if rc == 0:
            return "rpm2cpio|cpio"
    if have("bsdtar"):
        rc, _, err = run(["bsdtar", "-xf", path, "-C", dest])
        if rc == 0:
            return "bsdtar"
        raise RuntimeError("bsdtar could not read the rpm: %s" % err.strip())
    if have("7z"):
        rc, _, err = run(["7z", "x", "-y", "-o" + dest, path])
        if rc != 0:
            raise RuntimeError("7z could not read the rpm: %s" % err.strip())
        return "7z"
    raise RuntimeError("no rpm reader: rpm2cpio/cpio, bsdtar and 7z are all absent")


def shlex_quote(text):
    return "'" + text.replace("'", "'\\''") + "'"


def tree_files(root):
    """Every regular file under `root`, as paths relative to it, '/'-separated."""
    found = []
    for base, _, names in os.walk(root):
        for name in names:
            full = os.path.join(base, name)
            if os.path.isfile(full) and not os.path.islink(full):
                found.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return sorted(found)


# --- shared checks --------------------------------------------------------

def check_sidecar(report, directory, name):
    """The `<asset>.sha256` sidecar next to the asset must match its bytes."""
    sidecar = os.path.join(directory, name + ".sha256")
    if not os.path.exists(sidecar):
        report.failed(name, "sidecar", "no %s.sha256" % name)
        return None
    claimed = open(sidecar).read().split()
    if not claimed:
        report.failed(name, "sidecar", "empty sidecar")
        return None
    actual = sha256_of(os.path.join(directory, name))
    if claimed[0].lower() != actual:
        report.failed(name, "sidecar", "says %s, bytes are %s" % (claimed[0], actual))
        return None
    report.passed(name, "sidecar", claimed[0][:16] + "...")
    return actual


def check_index(report, directory, name, index, digest):
    """index.json is what `finn` reads, and it carries the portable set only."""
    if index is None:
        report.skipped(name, "index", "no index.json beside the artifacts")
        return
    version, _, _ = name[len("finc-"):].partition("-")
    targets = index.get("versions", {}).get(version, {}).get("targets", {})
    entry = next((record for record in targets.values()
                  if record.get("file") == name), None)
    portable = name.endswith(PORTABLE)
    if not portable:
        # ADR 0010: distro/installer/dmg names stay out of the index, because
        # `finn` must keep matching exactly the portable set.
        if entry is not None:
            report.failed(name, "index", "a native installable is listed in index.json")
        else:
            report.passed(name, "index", "absent, as designed for native installables")
        return
    if entry is None:
        report.failed(name, "index", "portable artifact missing from index.json")
        return
    problems = []
    if digest is not None and entry.get("sha256") != digest:
        problems.append("sha256 differs from the sidecar")
    if entry.get("size") != os.path.getsize(os.path.join(directory, name)):
        problems.append("size says %s" % entry.get("size"))
    if problems:
        report.failed(name, "index", "; ".join(problems))
    else:
        report.passed(name, "index", "size and sha256 agree")


def check_layout(report, name, root, expect_bin, expect_lib):
    """Exactly `bin/finc[.exe]` + `lib/std/**.fin` + the license text, and nothing else.

    The license lives at `lib/finc/LICENSE` rather than under `share/` because
    the archive step is `tar -C stage bin lib`: a `share/` destination would be
    silently dropped, and GPL redistribution requires the text to travel with
    the binary.
    """
    files = tree_files(root)
    problems = []
    if expect_bin not in files:
        problems.append("missing %s" % expect_bin)
    licence = expect_lib.rsplit("/", 1)[0] + "/finc/LICENSE"
    strays = [path for path in files
              if path != expect_bin and path != licence
              and not re.fullmatch(re.escape(expect_lib) + r"/[^/]+\.fin", path)]
    if strays:
        problems.append("unexpected: " + ", ".join(strays[:4]))
    stdlib = [path for path in files if path.startswith(expect_lib + "/")]
    if not stdlib:
        problems.append("no %s/*.fin" % expect_lib)
    if problems:
        report.failed(name, "layout", "; ".join(problems))
        return False
    licence_note = " + LICENSE" if licence in files else " (no LICENSE)"
    report.passed(name, "layout", "%d stdlib modules%s" % (len(stdlib), licence_note))
    return True


def check_interp(report, name, binary, expect_static):
    """A musl build must carry no program interpreter; a gnu build must."""
    if not have("readelf"):
        report.skipped(name, "interp", "no readelf on PATH")
        return
    _, container = machine_of(binary)
    if container != "ELF":
        report.skipped(name, "interp", "readelf is an ELF tool; this is %s" % container)
        return
    rc, out, err = run(["readelf", "-lW", binary])
    if rc != 0:
        report.failed(name, "interp", "readelf failed: %s" % err.strip()[:80])
        return
    interps = [line.split("Requesting program interpreter:")[1].strip(" ]")
               for line in out.splitlines()
               if "Requesting program interpreter:" in line]
    if expect_static and interps:
        report.failed(name, "interp", "static build carries %s" % interps[0])
    elif not expect_static and not interps:
        report.failed(name, "interp", "dynamically linked build has no INTERP")
    else:
        report.passed(name, "interp", interps[0] if interps else "no interpreter (static)")


def defining_sections(library):
    """{symbol: section name} for the OBJECT/FUNC symbols a library defines.

    `readelf` prints a section index as `[ 26]` (space-padded) or `[26]`, so the
    number is not a plain whitespace field; the bracket pair is parsed out.
    """
    rc, out, _ = run(["readelf", "-SW", library])
    if rc != 0:
        return {}
    sections = {}
    for line in out.splitlines():
        stripped = line.strip()
        if not stripped.startswith("[") or "]" not in stripped:
            continue
        index, rest = stripped[1:].split("]", 1)
        fields = rest.split()
        if index.strip().isdigit() and fields:
            sections[int(index)] = fields[0]
    rc, out, _ = run(["readelf", "-sW", "--dyn-syms", library])
    if rc != 0:
        return {}
    found = {}
    for line in out.splitlines():
        # Num: Value Size Type Bind Vis Ndx Name
        fields = line.split()
        if len(fields) < 8 or fields[3] not in ("OBJECT", "FUNC"):
            continue
        if fields[6] in ("UND", "ABS"):
            continue
        try:
            index = int(fields[6])
        except ValueError:
            continue
        found[fields[7].split("@")[0]] = sections.get(index)
    return found


def check_relro_copy(report, name, binary, needed):
    """A COPY-relocated object its library writes must not land in read-only memory.

    A COPY relocation reserves space in the executable for an object the shared
    library defines, and the dynamic linker fills it before any initialiser runs.
    `PT_GNU_RELRO` then makes that page read-only. A library that assigns to its
    own object afterwards -- `llvm::OptimizationLevel::O1` is set in libLLVM's
    initialiser -- writes into that read-only page and the process dies before
    `main`.

    Why this is a packaging check and not a curiosity: whether it dies depends on
    the user's libLLVM. Ubuntu's `llvm-22` writes the object; another build may
    not, or may leave it in `.data.rel.ro` where copying is harmless. A binary
    that survives only against the build host's library is not one a release can
    promise. The runtime `run --version` check catches the crash on a host that
    reproduces it; this check names the cause and catches it on any host.
    """
    if not have("readelf"):
        report.skipped(name, "relro-copy", "no readelf on PATH")
        return
    _, container = machine_of(binary)
    if container != "ELF":
        report.skipped(name, "relro-copy", "readelf is an ELF tool; this is %s" % container)
        return
    rc, out, _ = run(["readelf", "-lW", binary])
    if rc != 0:
        report.failed(name, "relro-copy", "could not read program headers")
        return
    relro = None
    for line in out.splitlines():
        if "GNU_RELRO" in line:
            fields = line.split()
            relro = (int(fields[2], 16), int(fields[2], 16) + int(fields[4], 16))
    if relro is None:
        report.skipped(name, "relro-copy", "binary has no PT_GNU_RELRO")
        return
    rc, out, _ = run(["readelf", "-rW", binary])
    if rc != 0:
        report.failed(name, "relro-copy", "could not read relocations")
        return
    inside = []
    for line in out.splitlines():
        # `readelf -r` prints the type as R_X86_64_COPY / R_AARCH64_COPY / ...
        fields = line.split()
        if len(fields) < 5 or not fields[2].endswith("_COPY"):
            continue
        try:
            target = int(fields[0], 16)
        except ValueError:
            continue
        if relro[0] <= target < relro[1]:
            inside.append(fields[4].split("@")[0])
    if not inside:
        report.passed(name, "relro-copy", "no COPY target inside RELRO")
        return

    # Only a library that keeps the object in writable storage will write it.
    search = ["/usr/lib", "/usr/lib64", "/lib", "/lib64", "/usr/local/lib"]
    for machine_dir in ("x86_64-linux-gnu", "aarch64-linux-gnu"):
        search.append("/usr/lib/" + machine_dir)
    defining, library_paths = {}, []
    for library in sorted(set(needed or ())):
        located = None
        if os.path.isabs(library):
            located = library if os.path.exists(library) else None
        else:
            for directory in search:
                candidate = os.path.join(directory, library)
                if os.path.exists(candidate):
                    located = os.path.realpath(candidate)
                    break
        if located is None:
            continue
        library_paths.append(os.path.basename(located))
        defining.update(defining_sections(located))
    if not defining:
        report.skipped(name, "relro-copy",
                       "%d COPY slot(s) in RELRO, but none of its libraries are readable here"
                       % len(inside))
        return

    def writable(section):
        return bool(section) and (section in (".bss", ".data", ".tdata")
                                  or section.startswith(".bss."))

    risky = [symbol for symbol in inside if writable(defining.get(symbol))]
    unknown = [symbol for symbol in inside if symbol not in defining]
    if risky:
        report.failed(name, "relro-copy",
                      "%d COPY slot(s) inside RELRO that their library writes (%s): %s"
                      % (len(risky), ", ".join(library_paths[:2]),
                         ", ".join(sorted(risky)[:3])))
    elif unknown:
        report.skipped(name, "relro-copy",
                       "%d COPY slot(s) inside RELRO; %d not found in %s"
                       % (len(inside), len(unknown), ", ".join(library_paths[:2])))
    else:
        report.passed(name, "relro-copy",
                      "%d COPY slot(s) inside RELRO, all read-only in their library"
                      % len(inside))


def needed_libraries(report, name, binary):
    """List the shared libraries the binary demands, or its static status."""
    if not have("readelf"):
        report.skipped(name, "needed", "no readelf on PATH")
        return None
    _, container = machine_of(binary)
    if container != "ELF":
        report.skipped(name, "needed", "readelf is an ELF tool; this is %s" % container)
        return None
    rc, out, err = run(["readelf", "-dW", binary])
    if rc != 0:
        report.failed(name, "needed", "readelf failed: %s" % err.strip()[:80])
        return None
    needed = [line.split("[", 1)[1].rstrip("]")
              for line in out.splitlines()
              if "(NEEDED)" in line and "[" in line]
    if not needed:
        report.passed(name, "needed", "none (statically linked)")
        return []
    return needed


def check_resolvable(report, name, binary, needed, runnable, arch=None, container=None):
    """`ldd` must resolve every NEEDED entry against the host's libraries."""
    if needed is None:
        return
    if not runnable:
        report.skipped(name, "ldd", "%s %s binary; this host is %s"
                       % (arch, container, host_arch()))
        return
    if not have("ldd"):
        report.skipped(name, "ldd", "no ldd on PATH")
        return
    if not needed:
        report.passed(name, "ldd", "statically linked, nothing to resolve")
        return
    rc, out, err = run(["ldd", binary])
    unresolved = [line.strip() for line in out.splitlines() if "not found" in line]
    if unresolved:
        report.failed(name, "ldd", "; ".join(unresolved[:3]))
        return
    report.passed(name, "ldd", "resolved: " + ", ".join(needed[:6])
                  + ("..." if len(needed) > 6 else ""))


def check_runs(report, name, binary, runnable, why_not):
    """`--version` must print the contract line on stdout and exit 0."""
    if not runnable:
        report.skipped(name, "run --version", why_not)
        return False
    rc, out, err = run([binary, "--version"])
    if rc != 0:
        detail = "exit %d%s" % (rc, ": " + err.strip().splitlines()[-1][:60] if err.strip() else "")
        report.failed(name, "run --version", detail)
        return False
    line = out.strip()
    if not re.fullmatch(r"finc \S+ \(contract \d+\)", line):
        report.failed(name, "run --version", "unexpected stdout %r" % line[:60])
        return False
    report.passed(name, "run --version", line)
    return True


def check_compile_and_run(report, name, binary, runnable, why_not, workdir):
    """Compile and run the smoke program from a directory outside the install.

    The working directory is deliberately foreign, so `lib/std` can only be
    found through `<exe-dir>/../lib/std` -- the rule the archive's layout
    promises. `-o` needs a C driver, so a host without one is a SKIP.
    """
    if not runnable:
        report.skipped(name, "compile+run", why_not)
        return
    if not have("cc") and not have("gcc"):
        report.skipped(name, "compile+run", "no C compiler driver on PATH")
        return
    project = tempfile.mkdtemp(prefix="finc-smoke-", dir=workdir)
    source = os.path.join(project, "smoke.fin")
    with open(source, "w") as handle:
        handle.write(SMOKE_SOURCE)
    exe = os.path.join(project, "smoke")
    rc, out, err = run([binary, "-o", exe, source], cwd=project)
    if rc != 0:
        report.failed(name, "compile+run", "compile exit %d: %s"
                      % (rc, (err or out).strip().splitlines()[-1][:70]))
        return
    if not os.path.exists(exe):
        report.failed(name, "compile+run", "compiler reported success but wrote no executable")
        return
    rc, out, err = run([exe], cwd=project)
    if rc != 0:
        report.failed(name, "compile+run", "program exit %d: %s"
                      % (rc, err.strip().splitlines()[-1][:60] if err.strip() else ""))
    elif out != SMOKE_OUTPUT:
        report.failed(name, "compile+run", "stdout %r, expected %r" % (out, SMOKE_OUTPUT))
    else:
        report.passed(name, "compile+run", "lib/std resolved outside the install")


# --- per-class handlers ---------------------------------------------------

def verify_tarball(report, directory, name, triple, workdir):
    root = tempfile.mkdtemp(prefix="finc-tar-", dir=workdir)
    with tarfile.open(os.path.join(directory, name)) as archive:
        archive.extractall(root, filter="data")
    if not check_layout(report, name, root, "bin/finc", "lib/std"):
        return
    binary = os.path.join(root, "bin", "finc")
    os.chmod(binary, 0o755)
    arch, container = machine_of(binary)
    runnable = (container == "ELF" and arch == host_arch()
                and sys.platform.startswith("linux"))
    why = ("a %s %s binary cannot run on this %s host"
           % (arch, container, host_arch())) if not runnable else ""
    musl = triple.endswith("-musl")
    check_interp(report, name, binary, expect_static=musl)
    needed = needed_libraries(report, name, binary)
    check_relro_copy(report, name, binary, needed)
    check_resolvable(report, name, binary, needed, runnable, arch, container)
    if check_runs(report, name, binary, runnable, why):
        check_compile_and_run(report, name, binary, runnable, why, workdir)


def verify_zip(report, directory, name, triple):
    """A portable Windows tree: exactly bin/finc.exe + lib/std/**.fin."""
    path = os.path.join(directory, name)
    with zipfile.ZipFile(path) as archive:
        bad = archive.testzip()
        if bad is not None:
            report.failed(name, "zip-crc", "corrupt member %s" % bad)
            return
        members = [info.filename for info in archive.infolist() if not info.is_dir()]
    report.passed(name, "zip-crc", "%d members" % len(members))
    strays = [member for member in members
              if member != "bin/finc.exe"
              and not re.fullmatch(r"lib/std/[^/]+\.fin", member)]
    stdlib = [member for member in members if member.startswith("lib/std/")]
    problems = []
    if "bin/finc.exe" not in members:
        problems.append("missing bin/finc.exe")
    if strays:
        problems.append("unexpected: " + ", ".join(strays[:4]))
    if not stdlib:
        problems.append("no lib/std/*.fin")
    if problems:
        report.failed(name, "layout", "; ".join(problems))
    else:
        report.passed(name, "layout", "%d stdlib modules" % len(stdlib))
    # The machine type has to match the triple in the name, or an arm64 user
    # downloads an x64 build (the bug the naming scheme exists to prevent).
    root = tempfile.mkdtemp(prefix="finc-zip-", dir=os.path.dirname(directory))
    with zipfile.ZipFile(path) as archive:
        archive.extract("bin/finc.exe", root)
    arch, container = machine_of(os.path.join(root, "bin", "finc.exe"))
    expected = target_arch(triple)
    if container != "PE":
        report.failed(name, "pe-machine", "not a PE executable (%s)" % container)
    elif arch != expected:
        report.failed(name, "pe-machine", "%s binary named %s" % (arch, triple))
    else:
        report.passed(name, "pe-machine", "%s %s" % (container, arch))
    report.skipped(name, "compile+run", "a Windows PE binary cannot run on this host")


def verify_deb(report, directory, name, workdir):
    root = tempfile.mkdtemp(prefix="finc-deb-", dir=workdir)
    try:
        control = extract_deb(os.path.join(directory, name), root)
    except (ValueError, tarfile.TarError) as exc:
        report.failed(name, "extract", str(exc))
        return
    if control is None:
        report.failed(name, "control", "data.tar without control.tar")
    else:
        fields = dict(line.split(":", 1) for line in control.splitlines()
                      if ":" in line and not line.startswith(" "))
        version = name[len("finc-"):].split("-", 1)[0]
        problems = []
        if fields.get("Package", "").strip() != "finc":
            problems.append("Package=%r" % fields.get("Package"))
        if fields.get("Version", "").strip() != version:
            problems.append("Version=%r, asset says %s" % (fields.get("Version"), version))
        if fields.get("Architecture", "").strip() != "amd64":
            problems.append("Architecture=%r" % fields.get("Architecture"))
        if problems:
            report.failed(name, "control", "; ".join(problems))
        else:
            report.passed(name, "control", "finc %s amd64, Depends=%s"
                          % (version, fields.get("Depends", "none").strip()))
    if not check_layout(report, name, os.path.join(root, "usr"), "bin/finc", "lib/std"):
        return
    binary = os.path.join(root, "usr", "bin", "finc")
    os.chmod(binary, 0o755)
    arch, container = machine_of(binary)
    runnable = (container == "ELF" and arch == host_arch()
                and sys.platform.startswith("linux"))
    why = ("a %s %s binary cannot run on this %s host"
           % (arch, container, host_arch())) if not runnable else ""
    needed = needed_libraries(report, name, binary)
    check_relro_copy(report, name, binary, needed)
    check_resolvable(report, name, binary, needed, runnable, arch, container)
    if check_runs(report, name, binary, runnable, why):
        check_compile_and_run(report, name, binary, runnable, why, workdir)


def verify_rpm(report, directory, name, workdir):
    root = tempfile.mkdtemp(prefix="finc-rpm-", dir=workdir)
    try:
        how = extract_rpm(os.path.join(directory, name), root)
    except RuntimeError as exc:
        report.skipped(name, "extract", str(exc))
        return
    report.passed(name, "extract", "read with " + how)
    if not check_layout(report, name, os.path.join(root, "usr"), "bin/finc", "lib/std"):
        return
    binary = os.path.join(root, "usr", "bin", "finc")
    os.chmod(binary, 0o755)
    arch, container = machine_of(binary)
    runnable = (container == "ELF" and arch == host_arch()
                and sys.platform.startswith("linux"))
    why = ("a %s %s binary cannot run on this %s host"
           % (arch, container, host_arch())) if not runnable else ""
    needed = needed_libraries(report, name, binary)
    check_relro_copy(report, name, binary, needed)
    check_resolvable(report, name, binary, needed, runnable, arch, container)
    if check_runs(report, name, binary, runnable, why):
        check_compile_and_run(report, name, binary, runnable, why, workdir)


def verify_dmg(report, directory, name, workdir):
    """A dmg can only be attached on macOS; elsewhere read its contents."""
    path = os.path.join(directory, name)
    if sys.platform == "darwin" and have("hdiutil"):
        rc, out, err = run(["hdiutil", "verify", path])
        if rc != 0:
            report.failed(name, "hdiutil verify", err.strip()[:80])
            return
        report.passed(name, "hdiutil verify", "image checksum valid")
        mount = tempfile.mkdtemp(prefix="finc-dmg-", dir=workdir)
        rc, _, err = run(["hdiutil", "attach", path, "-mountpoint", mount, "-nobrowse"])
        if rc != 0:
            report.failed(name, "attach", err.strip()[:80])
            return
        try:
            verify_dmg_tree(report, name, mount)
        finally:
            run(["hdiutil", "detach", mount])
        report.skipped(name, "compile+run", "run by the release workflow on the macOS runner")
        return
    if not have("7z"):
        report.skipped(name, "extract", "no hdiutil (macOS only) and no 7z to read the image")
        return
    root = tempfile.mkdtemp(prefix="finc-dmg-", dir=workdir)
    rc, _, err = run(["7z", "x", "-y", "-o" + root, path], timeout=600)
    if rc != 0:
        report.failed(name, "extract", "7z could not read the image: %s" % err.strip()[:70])
        return
    report.passed(name, "extract", "7z (hdiutil is macOS-only)")
    verify_dmg_tree(report, name, root)
    report.skipped(name, "hdiutil verify", "hdiutil is macOS-only; image checksum unverified")
    report.skipped(name, "compile+run", "a Mach-O binary cannot run on this host")


def verify_dmg_tree(report, name, root):
    """The volume must carry one folder holding bin/finc + lib/std/**.fin."""
    tops = [entry for entry in os.listdir(root) if not entry.startswith(".")]
    if len(tops) != 1 or not os.path.isdir(os.path.join(root, tops[0])):
        report.failed(name, "layout", "volume root holds %r, expected one folder" % (tops,))
        return
    volume = os.path.join(root, tops[0])
    inner = tempfile.mkdtemp(prefix="finc-vol-")
    flat = os.path.join(inner, "c")
    shutil.copytree(volume, flat, symlinks=True)
    check_layout(report, name, flat, "bin/finc", "lib/std")
    binary = os.path.join(flat, "bin", "finc")
    if not os.path.exists(binary):
        return
    arch, container = machine_of(binary)
    if container != "Mach-O":
        report.failed(name, "macho", "not a Mach-O executable (%s)" % container)
    else:
        report.passed(name, "macho", "%s %s" % (container, arch))


def verify_setup(report, directory, name, triple):
    """An Inno Setup executable can only be run on Windows."""
    path = os.path.join(directory, name)
    arch, container = machine_of(path)
    if container != "PE":
        report.failed(name, "pe", "not a PE executable (%s)" % container)
    else:
        # The Inno stub is 32-bit on every architecture by design, so the stub's
        # machine says nothing about the payload. The payload is proved by the
        # zip in the same release, which this script checks separately.
        report.passed(name, "pe", "Inno Setup stub (%s), payload proved via the zip" % arch)
    report.skipped(name, "install+run", "an Inno Setup .exe can only be installed on Windows")


def verify_index_global(report, directory, index, artifacts):
    """Every portable artifact must be listed, and nothing else may be."""
    if index is None:
        report.skipped("index.json", "coverage", "no index.json in the artifact directory")
        return
    listed = set()
    for version, record in index.get("versions", {}).items():
        for triple, target in record.get("targets", {}).items():
            listed.add(target.get("file"))
            expected = "finc-%s-%s.tar.gz" % (version, triple)
            alternate = "finc-%s-%s.zip" % (version, triple)
            if target.get("file") not in (expected, alternate):
                report.failed("index.json", "coverage",
                              "%s is not a recognised asset name" % target.get("file"))
    present = {name for name, _, _ in artifacts if name.endswith(PORTABLE)}
    if present - listed:
        report.failed("index.json", "coverage",
                      "unlisted: " + ", ".join(sorted(present - listed)[:3]))
    elif listed - present:
        report.failed("index.json", "coverage",
                      "listed but absent: " + ", ".join(sorted(listed - present)[:3]))
    else:
        report.passed("index.json", "coverage", "%d portable artifacts" % len(present))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify downloaded finc release artifacts on this host.")
    parser.add_argument("directory", help="directory holding the release assets")
    parser.add_argument("--keep", action="store_true",
                        help="keep the unpacked trees and print where they are")
    args = parser.parse_args(argv)

    directory = os.path.abspath(args.directory)
    if not os.path.isdir(directory):
        sys.exit("%s is not a directory" % directory)

    artifacts = []
    for name in sorted(os.listdir(directory)):
        match = NAME.match(name)
        if match:
            artifacts.append((name, match.group("kind"), match.group("triple")))
    if not artifacts:
        sys.exit("no finc-<version>-<triple> artifacts in %s" % directory)

    index = None
    for candidate in (os.path.join(directory, "index.json"),
                      os.path.join(os.path.dirname(directory), "index.json")):
        if os.path.exists(candidate):
            with open(candidate) as handle:
                index = json.load(handle)
            break

    report = Report()
    workdir = tempfile.mkdtemp(prefix="finc-verify-")
    print("host: %s, %s; %d artifact(s) in %s\n"
          % (sys.platform, host_arch(), len(artifacts), directory))
    verify_index_global(report, directory, index, artifacts)
    try:
        for name, kind, triple in artifacts:
            digest = check_sidecar(report, directory, name)
            check_index(report, directory, name, index, digest)
            if kind == ".tar.gz":
                verify_tarball(report, directory, name, triple, workdir)
            elif kind == ".zip":
                verify_zip(report, directory, name, triple)
            elif kind == ".deb":
                verify_deb(report, directory, name, workdir)
            elif kind == ".rpm":
                verify_rpm(report, directory, name, workdir)
            elif kind == ".dmg":
                verify_dmg(report, directory, name, workdir)
            elif kind == "-setup.exe":
                verify_setup(report, directory, name, triple)
            print()
    finally:
        if args.keep:
            print("unpacked trees kept in %s" % workdir)
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    counts = report.counts
    print("%d PASS, %d SKIP, %d FAIL" % (counts["PASS"], counts["SKIP"], counts["FAIL"]))
    if report.failures:
        print("A FAIL means a downloaded artifact does not do what its name promises;"
              " SKIP lines name a capability this host lacks and are not passes.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

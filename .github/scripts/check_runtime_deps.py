#!/usr/bin/env python3
"""Report what the shipped binary needs at run time, and refuse an undocumented one.

WHY THIS IS A RELEASE GATE
--------------------------
A binary that runs on the machine that built it proves almost nothing about a machine that
did not. The v1.0.0 glibc artifacts were fine on the build host and segfaulted everywhere
else; the v1.0.0 macOS artifacts link

    /opt/homebrew/opt/llvm@22/lib/libLLVM.dylib

by absolute path, so `finc` there will not start unless Homebrew is at that exact prefix.
Both defects pass `"$binary" --version` on the runner, because the runner is the one machine
where the dependency is present.

So this reads the dependency list out of the binary and compares it against what the
documentation promises. An unlisted non-system library is an error, not a note: it means an
artifact that only works on machines shaped like the build machine.

Run against a packaged binary:

    python3 .github/scripts/check_runtime_deps.py stage/bin/finc --target <triple>

The allowed set per platform is in ALLOWED below, and is the same table INSTALL.md carries.
Changing one without the other is the bug this guards.
"""

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# Libraries the documentation promises, by target family. A name here is a documented
# requirement the user is told to install; anything else is a mistake.
SYSTEM = {
    # Present on every supported Linux; part of the base system, not an extra install.
    "linux": {
        "libc.so.6", "libm.so.6", "libgcc_s.so.1", "libstdc++.so.6",
        "libz.so.1", "libzstd.so.1", "libtinfo.so.6", "libffi.so.8",
        "libedit.so.0", "libxml2.so.16", "libxml2.so.2", "liblzma.so.5",
        "libncursesw.so.6", "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1",
        "linux-vdso.so.1", "libc.musl-x86_64.so.1",
    },
    # macOS: the system libraries live in /usr/lib and the dyld shared cache.
    "darwin": {
        "/usr/lib/libSystem.B.dylib", "/usr/lib/libc++.1.dylib",
        "/usr/lib/libobjc.A.dylib", "/usr/lib/libresolv.9.dylib",
        "/usr/lib/libz.1.dylib", "/usr/lib/libiconv.2.dylib",
        "/usr/lib/libncurses.5.4.dylib",
    },
}

# A documented, user-installable dependency. Matched as a path prefix so the version
# directory can move; the doc says "the llvm-22 package" or "brew install llvm@22".
DOCUMENTED = {
    "linux": ("libLLVM.so",),
    "darwin": ("/opt/homebrew/opt/llvm@22/", "/usr/local/opt/llvm@22/"),
}


def elf_needed(path: Path) -> list[str]:
    """DT_NEEDED entries from the dynamic section, without invoking ldd.

    `ldd` executes the binary's loader on some systems, and it cannot report a completely
    static binary as such without erroring. Reading .dynamic is safe and exact.
    """
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        return []
    is64 = data[4] == 2
    little = data[5] == 1
    endian = "<" if little else ">"
    if is64:
        e_phoff = struct.unpack_from(endian + "Q", data, 0x20)[0]
        e_phentsize = struct.unpack_from(endian + "H", data, 0x36)[0]
        e_phnum = struct.unpack_from(endian + "H", data, 0x38)[0]
    else:
        e_phoff = struct.unpack_from(endian + "I", data, 0x1C)[0]
        e_phentsize = struct.unpack_from(endian + "H", data, 0x2A)[0]
        e_phnum = struct.unpack_from(endian + "H", data, 0x2C)[0]

    loads = []  # (vaddr, offset, filesz)
    dynamic = None
    for i in range(e_phnum):
        base = e_phoff + i * e_phentsize
        p_type = struct.unpack_from(endian + "I", data, base)[0]
        if p_type == 1:  # PT_LOAD
            off = struct.unpack_from(endian + "Q", data, base + 8)[0]
            vaddr = struct.unpack_from(endian + "Q", data, base + 16)[0]
            filesz = struct.unpack_from(endian + "Q", data, base + 32)[0]
            loads.append((vaddr, off, filesz))
        elif p_type == 2:  # PT_DYNAMIC
            off = struct.unpack_from(endian + "Q", data, base + 8)[0]
            filesz = struct.unpack_from(endian + "Q", data, base + 32)[0]
            dynamic = (off, filesz)

    if dynamic is None:
        return []  # static: nothing needed, which is a valid and desirable answer

    def vaddr_to_offset(addr: int) -> int | None:
        for vaddr, off, filesz in loads:
            if vaddr <= addr < vaddr + filesz:
                return off + (addr - vaddr)
        return None

    strtab = None
    needed_offsets = []
    dyn_off, dyn_sz = dynamic
    step = 16 if is64 else 8
    for pos in range(dyn_off, dyn_off + dyn_sz, step):
        if is64:
            tag, val = struct.unpack_from(endian + "qQ", data, pos)
        else:
            tag, val = struct.unpack_from(endian + "iI", data, pos)
        if tag == 0:  # DT_NULL
            break
        if tag == 1:  # DT_NEEDED
            needed_offsets.append(val)
        elif tag == 5:  # DT_STRTAB
            strtab = vaddr_to_offset(val)

    if strtab is None:
        return []
    names = []
    for off in needed_offsets:
        end = data.index(b"\0", strtab + off)
        names.append(data[strtab + off : end].decode(errors="replace"))
    return names


def macho_dylibs(path: Path) -> list[str]:
    """LC_LOAD_DYLIB / weak / re-export load-command paths."""
    data = path.read_bytes()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0xFEEDFACF:
        return []
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off = 32
    out = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd in (0xC, 0xD, 0x18F, 0x1F):  # LOAD / WEAK / REEXPORT / LAZY
            stroff = struct.unpack_from("<I", data, off + 8)[0]
            end = data.index(b"\0", off + stroff)
            out.append(data[off + stroff : end].decode(errors="replace"))
        off += size
    return out


def classify(name: str, family: str) -> str | None:
    """'system', 'documented', or None when it is undocumented."""
    if family == "darwin":
        if name in SYSTEM["darwin"] or name.startswith("/usr/lib/"):
            return "system"
        for prefix in DOCUMENTED["darwin"]:
            if name.startswith(prefix):
                return "documented"
        return None
    # Linux matches on the soname, which carries the version.
    base = name.rsplit("/", 1)[-1]
    if base in SYSTEM["linux"]:
        return "system"
    for prefix in DOCUMENTED["linux"]:
        if base.startswith(prefix):
            return "documented"
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("binary", type=Path)
    ap.add_argument("--target", required=True, help="the release triple, for reporting")
    args = ap.parse_args()

    family = "darwin" if "apple-darwin" in args.target else "linux"
    if "windows" in args.target:
        # PE dependencies are not read here; the Windows builds link LLVM statically and
        # ship with the MSVC runtime, and `dumpbin` is the tool for them. Say so rather
        # than implying a check happened.
        print(f"runtime deps: {args.target}: not inspected (Windows PE imports are not read "
              f"by this script; the Windows builds link LLVM in)")
        return 0

    if not args.binary.exists():
        print(f"error: {args.binary} does not exist", file=sys.stderr)
        return 1

    names = macho_dylibs(args.binary) if family == "darwin" else elf_needed(args.binary)

    if not names:
        print(f"runtime deps: {args.target}: none (statically linked)")
        return 0

    undocumented = []
    for name in sorted(set(names)):
        kind = classify(name, family)
        if kind is None:
            undocumented.append(name)
            print(f"  UNDOCUMENTED  {name}")
        else:
            print(f"  {kind:<12}  {name}")

    if undocumented:
        print(
            f"\nerror: {args.binary.name} needs {len(undocumented)} library the documentation "
            f"does not promise:\n  " + "\n  ".join(undocumented) + "\n"
            "Either teach the user to install it (INSTALL.md and the site's downloads section "
            "carry the same table this script enforces), or link it statically. Shipping it "
            "without saying so is how a binary that only works on the build machine reaches "
            "a user.",
            file=sys.stderr,
        )
        return 1

    print(f"runtime deps: {args.target}: every non-system library is documented")
    return 0


if __name__ == "__main__":
    sys.exit(main())

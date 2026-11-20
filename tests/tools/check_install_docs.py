#!/usr/bin/env python3
"""Run the shell blocks in INSTALL.md that say they can be run.

INSTALL.md makes claims a reader will act on ("run this, you get that"), and a
claim nobody executes drifts. This turns every block carrying an execution
marker into a test, and refuses to let a block be marked ambiguously:

  <!-- fin-verify -->                  run it here, with `finc` on PATH
  <!-- fin-verify-asset: <glob> -->    run it here, with release assets from
                                       --assets copied in; SKIP cleanly (with a
                                       reason) when --assets is absent or has no
                                       match, because CI has no published
                                       release to download from
  <!-- fin-unsupported: <reason> -->   never run; the reason is required, and is
                                       printed so a skip is visible rather than
                                       silent

A fenced `sh` block with no marker at all is a failure: "somebody forgot" and
"deliberately prose" look identical otherwise, and the failure mode this file
exists to stop is a command that was never executed being read as one that was.

Expected output is asserted from the block itself, by comment:

  # expect: <substring>       must appear in stdout+stderr
  # expect-exit: <n>          the block's exit status (default 0)

Every block runs in its own temporary directory with the compiler's directory
prepended to PATH for that process only. Nothing writes outside the temporary
directory: no sudo, no system install, no edit to the shell's environment.

Usage:
    python3 tests/tools/check_install_docs.py --finc build/finc
    python3 tests/tools/check_install_docs.py --finc build/finc \\
        --assets /path/to/dir/holding/finc-<v>-<triple>.{tar.gz,deb,rpm}

Exit status: 0 all runnable blocks passed (skips are reported), 1 otherwise.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "INSTALL.md"

BLOCK = re.compile(
    r"(?P<marker><!--\s*(?P<kind>fin-verify-asset|fin-verify|fin-unsupported)"
    r"(?::\s*(?P<detail>[^\n]*?))?\s*-->[ \t]*\n)?"
    r"```sh\n(?P<body>.*?)^```[ \t]*(?:\n|$)",
    re.M | re.S,
)
EXPECT = re.compile(r"^[ \t]*#[ \t]*expect:[ \t]*(?P<text>.*?)[ \t]*$", re.M)
EXPECT_EXIT = re.compile(r"^[ \t]*#[ \t]*expect-exit:[ \t]*(?P<code>-?\d+)[ \t]*$", re.M)


def assets_for(directory, pattern):
    if directory is None:
        return None, "--assets was not given"
    matched = sorted(p for p in directory.glob(pattern) if p.is_file())
    if not matched:
        return None, "%s has no file matching %s" % (directory, pattern)
    if len(matched) > 1:
        return None, "%s matches %d files (%s); the block would be ambiguous" % (
            pattern, len(matched), ", ".join(p.name for p in matched))
    return matched[0], None


def run_block(body, workdir, path_prefix):
    env = dict(os.environ)
    env["PATH"] = str(path_prefix) + os.pathsep + env.get("PATH", "")
    # A release install is being simulated: leave no user configuration in play.
    for variable in ("FIN_LIBS", "FIN_CC", "FIN_LDFLAGS", "FIN_REF", "FIN_DIR"):
        env.pop(variable, None)
    # HOME and the XDG roots point inside the temporary directory, so a block
    # that installs to "$HOME/..." installs where this run will delete it and
    # nowhere else.
    home = workdir / "home"
    home.mkdir(exist_ok=True)
    env["HOME"] = str(home)
    env["XDG_CONFIG_HOME"] = str(home / ".config")
    env["XDG_DATA_HOME"] = str(home / ".local" / "share")
    env["NO_COLOR"] = "1"
    return subprocess.run(
        ["sh", "-c", body],
        cwd=workdir,
        env=env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=180,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--finc", required=True, type=Path,
                        help="compiler to put on PATH for the fin-verify blocks")
    parser.add_argument("--assets", type=Path, default=None,
                        help="directory holding release assets for fin-verify-asset blocks")
    args = parser.parse_args()

    if os.name != "posix":
        print("SKIP: INSTALL.md's shell blocks are POSIX sh; run this on Linux or macOS")
        return 0

    compiler = args.finc.resolve()
    if not compiler.is_file():
        print("FAIL --finc %s: not a file" % compiler)
        return 1
    finc_dir = compiler.parent

    contents = DOC.read_text(encoding="utf-8")
    blocks = list(BLOCK.finditer(contents))
    fenced = len(re.findall(r"^```sh$", contents, re.M))
    if len(blocks) != fenced:
        print("FAIL INSTALL.md: %d ```sh fences but %d parsed; a fence is malformed"
              % (fenced, len(blocks)))
        return 1

    failures = []
    ran = skipped = 0
    seen_assets = set()

    for block in blocks:
        line = contents.count("\n", 0, block.start()) + 1
        kind = block["kind"]
        detail = block["detail"]
        label = "INSTALL.md:%d" % line
        body = block["body"]

        if kind is None:
            failures.append("%s: ```sh block with no marker. Add `<!-- fin-verify -->`, "
                            "`<!-- fin-verify-asset: <glob> -->` or "
                            "`<!-- fin-unsupported: <reason> -->` before it." % label)
            continue

        if kind == "fin-unsupported":
            if not detail:
                failures.append("%s: fin-unsupported needs a reason after the colon" % label)
                continue
            skipped += 1
            print("SKIP %s (not run: %s)" % (label, detail))
            continue

        assets = []
        if kind == "fin-verify-asset":
            if not detail:
                failures.append("%s: fin-verify-asset needs an asset glob" % label)
                continue
            asset, why = assets_for(args.assets, detail)
            if asset is None:
                skipped += 1
                print("SKIP %s (asset %s: %s)" % (label, detail, why))
                continue
            assets = [asset]
            seen_assets.add(asset.name)

        with tempfile.TemporaryDirectory(prefix="fin-install-docs-") as temporary:
            workdir = Path(temporary)
            for asset in assets:
                shutil.copy2(asset, workdir / asset.name)
                sidecar = asset.with_name(asset.name + ".sha256")
                if sidecar.is_file():
                    shutil.copy2(sidecar, workdir / sidecar.name)

            short = " ".join(body.split())[:70]
            try:
                result = run_block(body, workdir, finc_dir)
            except subprocess.TimeoutExpired:
                failures.append("%s: timed out after 180s: %s" % (label, short))
                continue

            combined = result.stdout + result.stderr
            expected_exit = EXPECT_EXIT.search(body)
            want = int(expected_exit["code"]) if expected_exit else 0
            problems = []
            if result.returncode != want:
                problems.append("exit %d, expected %d" % (result.returncode, want))
            for expected in EXPECT.finditer(body):
                if expected["text"] not in combined:
                    problems.append("output did not contain %r" % expected["text"])
            if problems:
                failures.append("%s: %s\n  block: %s\n  output:\n%s"
                                % (label, "; ".join(problems), short,
                                   "\n".join("    " + l for l in
                                             combined.splitlines()[:20])))
                continue

        ran += 1
        print("PASS %s : %s" % (label, short))

    if args.assets is not None:
        unused = sorted(p.name for p in args.assets.glob("finc-*")
                        if p.name not in seen_assets
                        and not p.name.endswith(".sha256"))
        for name in unused:
            print("WARN --assets: %s was not used by any block" % name)

    if failures:
        print()
        for failure in failures:
            print("FAIL " + failure)
        print("\n%d blocks ran, %d skipped, %d FAILED"
              % (ran, skipped, len(failures)))
        return 1

    if not ran:
        print("FAIL INSTALL.md: no block was executed; every one was skipped")
        return 1

    print("\n%d blocks ran, %d skipped (each printed with its reason)"
          % (ran, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check Fin fences in the application docs using an explicitly chosen compiler."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FENCE = re.compile(r"^```fin(?:[ \t]+([^\n]*))?\n(.*?)^```[ \t]*(?:\n|$)", re.M | re.S)
OUTPUT = re.compile(r"\s*```output\n(.*?)^```", re.M | re.S)

# docs/adr/ is binding (CLAUDE.md), but until issue #106 nothing checked a cited
# number against the directory. These checks run whenever the docs gate runs so
# a ruling cannot be cited with nothing behind it -- the cost ADR 0022's text
# already paid once: "A number cited with nothing behind it is a dangling
# reference, and the next reader spends their time discovering that rather than
# reading the decision."
#
#   * every `ADR NNNN` written in src/, tests/, docs/ or lib/ resolves to a file
#     in docs/adr/, or to a number that a doc in docs/ documents as reserved
#     (the visible one is 0024, reserved in ADR 0025's text);
#   * docs/adr/ files are named NNNN-name.md, numbers are unique, and numbering
#     has no gap that is not a documented reservation;
#   * the ruling count in CLAUDE.md matches the directory, so the number read
#     first cannot drift again the way it had (42 stated against 55 actual at
#     the fix).

ADR_DIR = ROOT / "docs/adr"
ADR_CITED = re.compile(r"\bADR[ \t]+(\d{4})\b")
ADR_NAME = re.compile(r"^(\d{4})-[0-9a-z-]+\.md$")
ADR_RESERVED = re.compile(
    r"\b(?:deliberately skipped|is reserved|reserved for|reserves)\b")
CLAUDE_COUNT = re.compile(r"`docs/adr/`\s*[—–-]+\s*(\d+) binding numbered rulings")

# Reading is textual under every subtree the citations come from; these are the
# binary shapes met in the tree, and any other binary noise decodes as
# replacement characters, which is harmless here.
NON_TEXT_SUFFIXES = {
    ".png", ".jpg", ".jpeg", ".gif", ".ico", ".woff", ".woff2", ".exe", ".dll",
    ".o", ".obj", ".a", ".so", ".bin", ".pyc", ".zip", ".gz", ".7z", ".ll",
}


def check_adr_corpus():
    filings = {}
    for path in sorted(ADR_DIR.glob("*.md")):
        # README.md is the index, not a filing: exempted so the numbering
        # corpus stays exactly the NNNN-name.md rulings it points at via links.
        if path.name == "README.md":
            continue
        naming = ADR_NAME.match(path.name)
        if not naming:
            raise SystemExit(
                f"FAIL docs/adr/{path.name}: not the NNNN-name.md numbering "
                "the corpus table reads by")
        number = naming[1]
        if number in filings:
            raise SystemExit(
                f"FAIL docs/adr/: two filings for {number} "
                f"({filings[number]} and {path.name}); a cited number must name "
                "one ruling")
        filings[number] = path.name
    if not filings:
        raise SystemExit("FAIL docs/adr/: no ADR filings found")

    cited = {}
    reserved = set()
    scanned = 0
    for subtree in ("src", "tests", "lib", "docs"):
        base = ROOT / subtree
        for path in sorted(base.rglob("*")):
            if path.is_dir() or "__pycache__" in path.parts or \
                    path.suffix.lower() in NON_TEXT_SUFFIXES:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            scanned += 1
            from_docs = subtree == "docs"
            for i, line in enumerate(text.splitlines(), 1):
                for number in ADR_CITED.findall(line):
                    place = f"{path.relative_to(ROOT)}:{i}"
                    if number not in cited:
                        cited[number] = place
                    if from_docs and ADR_RESERVED.search(line):
                        reserved.add(number)
    # A citation resolving to a reserved number is allowed -- being documented
    # in docs/ is what makes it resolvable.
    for number in sorted(cited):
        if number not in filings and number not in reserved:
            first = cited[number]
            raise SystemExit(
                f"FAIL {first}: cites ADR {number}, which resolves to no file in "
                "docs/adr/ and no documented reservation. Point the citation at "
                "the real ruling, or document the reservation in docs/.")

    gaps = sorted(set(f"{k:04d}" for k in range(1, max(map(int, filings)) + 1)) - set(filings))
    for number in gaps:
        if number not in reserved:
            raise SystemExit(
                f"FAIL docs/adr/: numbering jumps over {number} with no file and "
                "no documented reservation (see ADR 0022's note on jumping from "
                "0020 to 0023). Document the gap in docs/ or leave no gap.")

    claude = ROOT / "CLAUDE.md"
    stated = None
    for i, line in enumerate(claude.read_text(encoding="utf-8").splitlines(), 1):
        counted = CLAUDE_COUNT.search(line)
        if counted:
            stated = (counted[1], i)
    if stated is None:
        raise SystemExit(
            'FAIL CLAUDE.md: the "`docs/adr/` — N binding numbered rulings" '
            f"element is gone; tests/tools/check_docs.py verifies it against "
            "docs/adr/, so it is restored with the real count when written")
    actual = len(filings)
    if int(stated[0]) != actual:
        raise SystemExit(
            f"FAIL CLAUDE.md:{stated[1]}: states {stated[0]} binding rulings in "
            f"docs/adr/, which holds {actual}. Update the number -- it is "
            "checked against the directory every time this gate runs.")
    print(f"PASS docs/adr: {actual} rulings, {len(cited)} ADR numbers cited and "
          f"resolved across {scanned} files scanned; documented reservations: "
          f"{sorted(reserved)}")
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--finc", required=True, type=Path)
    args = parser.parse_args()
    check_adr_corpus()
    compiler = args.finc.resolve()
    env = {**os.environ, "FIN_LIBS": str(ROOT / "lib/std")}
    docs = [ROOT / "README.md", ROOT / "docs/agent-guide.md"]
    docs += sorted((ROOT / "docs/guide").glob("*.md"))
    checked = fragments = 0

    def invoke(command, expected=0):
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", env=env, timeout=60)
        if result.returncode != expected:
            raise RuntimeError(f"{command}\nexit {result.returncode}, expected {expected}"
                               f"\n{result.stdout}{result.stderr}")
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="fin-docs-") as temporary:
        directory = Path(temporary)
        for doc in docs:
            contents = doc.read_text(encoding="utf-8")
            blocks = list(FENCE.finditer(contents))
            if len(blocks) != len(re.findall(r"^```fin(?:[ \t].*)?$", contents, re.M)):
                raise SystemExit(f"Unclosed Fin fence in {doc.relative_to(ROOT)}")
            for block in blocks:
                mode = block[1] or "run"
                line = contents.count("\n", 0, block.start()) + 1
                label = f"{doc.relative_to(ROOT)}:{line} ({mode})"
                if mode == "fragment":
                    fragments += 1
                    continue
                if mode not in {"run", "check", "error", "build-error"}:
                    raise RuntimeError(f"{label}: unknown example mode")
                source = directory / "example.fin"
                executable = directory / ("example.exe" if os.name == "nt" else "example")
                source.write_text(block[2], encoding="utf-8")
                try:
                    invoke([str(compiler), str(source)], 1 if mode == "error" else 0)
                    if mode in {"run", "build-error"}:
                        invoke([str(compiler), str(source), "-o", str(executable)],
                               1 if mode == "build-error" else 0)
                    if mode == "run":
                        actual = invoke([str(executable)])
                        output = OUTPUT.match(contents, block.end())
                        if output and actual != output[1]:
                            raise RuntimeError(f"stdout {actual!r}, expected {output[1]!r}")
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                    raise SystemExit(f"FAIL {label}\n{error}") from error
                checked += 1
                print(f"PASS {label}", flush=True)
        for source in sorted((ROOT / "docs/examples").rglob("main.fin")):
            executable = directory / ("project.exe" if os.name == "nt" else "project")
            try:
                invoke([str(compiler), str(source)])
                invoke([str(compiler), str(source), "-o", str(executable)])
                actual = invoke([str(executable)])
                expected = source.with_suffix(".out").read_text(encoding="utf-8")
                if actual != expected:
                    raise RuntimeError(f"stdout {actual!r}, expected {expected!r}")
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                raise SystemExit(f"FAIL {source.relative_to(ROOT)}\n{error}") from error
            checked += 1
            print(f"PASS {source.relative_to(ROOT)}", flush=True)
    if not checked:
        raise SystemExit("No documentation examples found")
    print(f"{checked} examples passed; {fragments} explicitly marked fragments skipped")


if __name__ == "__main__":
    main()

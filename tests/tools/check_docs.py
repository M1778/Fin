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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--finc", required=True, type=Path)
    args = parser.parse_args()
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

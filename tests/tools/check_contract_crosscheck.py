#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the version parser actually used by the scheduled contract workflow."""

from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    workflow = root / ".github/workflows/contract-crosscheck.yml"
    text = workflow.read_text(encoding="utf-8")
    assignment = next(line for line in text.splitlines() if "finc_contract=" in line)
    match = re.search(r"sed -n '([^']+)'", assignment)
    assert match, "workflow must expose its sed version parser to this regression"
    cases = [
        ("finc 0.4.0 (contract 3)\n", "3\n"),
        ("finc 1.0.0 (contract 3)\n", "3\n"),
        ("finc 10.20.30 (contract 12)\n", "12\n"),
        ("finc 1.0.0 (contract )\n", ""),
        ("finc 1.0.0 (contract three)\n", ""),
        ("finc 1.0.0\n", ""),
        ("other 1.0.0 (contract 3)\n", ""),
        ("finc 1.0.0 (contract 3) trailing\n", ""),
    ]
    for version, expected in cases:
        result = subprocess.run(["sed", "-n", match[1]], input=version,
                                capture_output=True, text=True, check=False)
        assert result.returncode == 0, result.stderr
        assert result.stdout == expected, (version, result.stdout, expected)
    print(f"check_contract_crosscheck: {len(cases)} version spellings passed")


if __name__ == "__main__":
    main()

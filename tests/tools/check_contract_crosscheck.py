#!/usr/bin/env python3
# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️ for someone special.
#
# This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
# Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
# Fin is the name of the Angel from BlackNightmare-Stories not yet published.
# “Love is the reason this project exists”
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778/Fin
# Profile: https://github.com/M1778M/
# -----------------------------------------------------------------------------
# Copyright (C) 2027 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================

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

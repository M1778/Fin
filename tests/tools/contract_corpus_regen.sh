#!/bin/bash
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

# Contract corpus regen: rewrites every case's expected.jsonl from the real finc.
#
# Usage: tests/tools/contract_corpus_regen.sh [path/to/finc]
#
# tests/contract-corpus is the golden (argv, expected JSONL) pair set shared with
# `finn` (issue #87): cases/argv are in the corpus, and expected.jsonl comes from
# the real compiler. A run that only moves message text is a routine regen; a run
# that changes a key, a kind, a severity or an exit code is a contract change and
# needs docs/finc-interface-contract.md (and likely a `finn` side) updated with it.
set -u

finc="${1:-./build/finc}"
corpus="$(cd "$(dirname "$0")/../contract-corpus" && pwd)"
if [ ! -x "$finc" ]; then
    echo "contract_corpus_regen: finc '$finc' is not executable" >&2
    exit 1
fi

changed=0
for case_dir in "$corpus"/*/; do
    name="$(basename "${case_dir%/}")"
    abs="${case_dir%/}"
    args=()
    while IFS= read -r arg; do
        args+=("${arg//@DIR@/$abs}")
    done < "${case_dir}argv"
    # stderr only: stdout is reserved by the contract and is expected empty.
    out="$("$finc" "${args[@]}" 2>&1 1>/dev/null)"
    native_abs="$abs"
    if command -v cygpath >/dev/null 2>&1; then
        native_abs="$(cygpath -m "$abs")" || exit 1
    fi
    out="$(printf '%s' "$out" | python3 -c '
import re
import sys

directory = sys.argv[1].replace("\\", "/")
separator = r"(?:/|\\\\)"
pattern = separator.join(re.escape(part) for part in directory.split("/"))
if len(directory) > 1 and directory[1] == ":" and directory[0].isascii() and directory[0].isalpha():
    pattern = "[" + directory[0].lower() + directory[0].upper() + "]" + pattern[1:]
output = re.sub("(?:" + pattern + "|@DIR@)" + separator, "@DIR@/", sys.stdin.buffer.read().decode("utf-8").replace("\r", ""))
sys.stdout.buffer.write(output.encode("utf-8"))
' "$native_abs")" || exit 1
    printf '%s\n' "$out" > "${case_dir}expected.jsonl"
    last_line="$(tail -n 1 "${case_dir}expected.jsonl")"
    case "$last_line" in
        *'"kind":"summary"'*) : ;;
        *) echo "contract_corpus_regen: WARNING $name did not end in a summary" >&2 ;;
    esac
    changed=$((changed + 1))
    echo "regenerated $name"
done
echo "contract_corpus_regen: $changed cases regenerated from $finc"

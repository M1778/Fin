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

# Corpus snapshot: one line per sample -- exit code, diagnostic count, first diagnostic.
#
# Usage: tests/tools/corpus_snapshot.sh /tmp/snap.txt [path/to/finc]
#
# This is the measurement behind the "corpus diagnostics" figures in docs/plan.md. It is
# a ranking aid, not a test: the suite's authority is the `//@` expectation in each
# sample (ADR 0008), and a snapshot moving is not by itself a pass or a fail. What it is
# good for is deciding what to work on next -- the histogram of
#
#     for f in tests/samples/*.fin tests/samples/stdlib/*.fin; do ./build/finc "$f"; done \
#       2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep -E '^error:' \
#       | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn
#
# names the largest cascade, and a cascade is usually one bug with a multiplier.
#
# The count is anchored at `^error:` deliberately. An unanchored `grep -c 'error:'` also
# matches the source line echoed under the caret, so `stdlib/error.fin` -- a sample whose
# subject is error handling -- inflated the total by six. The figures up to and including
# the "One unresolved type" section of docs/plan.md were taken with the unanchored form
# and read about six high; every figure after it uses this script.
set -u
out="${1:?usage: corpus_snapshot.sh OUT [FINC]}"
finc="${2:-./build/finc}"
: > "$out"
total=0
for f in tests/samples/*.fin tests/samples/stdlib/*.fin; do
  # finc's own status, not the pipeline's. `cmd | sed` puts sed's status in $?, so the
  # rc column read 0 for every sample -- including the ones that print diagnostics and
  # exit 1 -- and was silently useless. Strip the colour in a second step instead.
  e=$("$finc" "$f" 2>&1); rc=$?
  e=$(printf '%s\n' "$e" | sed 's/\x1b\[[0-9;]*m//g')
  n=$(printf '%s\n' "$e" | grep -cE '^error:')
  first=$(printf '%s\n' "$e" | grep -E '^error:' | head -1 | cut -c1-70)
  total=$((total + n))
  printf '%-46s rc=%-3s n=%-4s %s\n' "$f" "$rc" "$n" "$first" >> "$out"
done
printf 'TOTAL %s diagnostics over %s samples\n' "$total" "$(wc -l < "$out")" | tee -a "$out"

#!/bin/bash
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
  e=$("$finc" "$f" 2>&1 | sed 's/\x1b\[[0-9;]*m//g'); rc=$?
  n=$(printf '%s\n' "$e" | grep -cE '^error:')
  first=$(printf '%s\n' "$e" | grep -E '^error:' | head -1 | cut -c1-70)
  total=$((total + n))
  printf '%-46s rc=%-3s n=%-4s %s\n' "$f" "$rc" "$n" "$first" >> "$out"
done
printf 'TOTAL %s diagnostics over %s samples\n' "$total" "$(wc -l < "$out")" | tee -a "$out"

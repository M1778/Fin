#!/bin/bash
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
    out="${out//$abs/@DIR@}"
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

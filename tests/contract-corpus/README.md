# finc contract corpus

The versioned CLI contract that `finc` and `finn` share is
[docs/finc-interface-contract.md](../../docs/finc-interface-contract.md); this
directory is the executable half of it (issue #87). One directory per case:

- `argv` — one argument per line, run verbatim by both consumers. `@DIR@` is the
  case directory itself, so fixtures stay machine-independent. No shell
  quoting, no shell: a consumer literally builds this argv.
- `input.fin` — the source the case compiles, referenced as `@DIR@/input.fin`.
  `exit-2-unreadable-file` has none by design: its subject is a file that does
  not exist.
- `expected.jsonl` — the JSONL the real compiler writes to stderr for that
  argv, with `@DIR@` normalized back into the token. Every case ends in a
  `summary` line with the exitCode, because that is how a consumer tells
  finished from dead.

Regenerate after a deliberate compiler change:

```sh
tests/tools/contract_corpus_regen.sh build/finc
```

A regen that only moves message text is routine. A regen that moves a key, a
kind, a severity or an exit code is a contract change: document it, and check
whether `finn` needs the same change — the corpus covers exit 0/1/2, and exit
3 (a compiler failure carrying its own diagnostic) is verified by injection,
not reachable from ordinary argv.

Which tests consume this: `tests/test_infra_contract.cpp` replays each case
against `build/finc` (`ContractCorpus.*`) and asserts the documented schema
independently of the corpus (`ContractConformance.*`). `finn` keeps the same
directory as parser fixtures for its `src/finc.rs` module; the weekly
cross-repo workflow runs both suites plus a contract-integer parity check
against `SUPPORTED_CONTRACT` there.

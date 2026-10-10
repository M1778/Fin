# Linguist evidence log (for #50)

Gate for opening the Linguist PR: **>=200 unique repos with `.fin` files
written in Fin, OR >=1000 `.fin` files indexed**, plus released `fin-vscode`
/ `tree-sitter-fin` with green CI and 3 months of keyword stability.
Do NOT open the PR before the gate (Linguist closes usage-poor bids fast).

## 2026-10-11 — baseline (LG1)

- `gh api search/code?q=extension:fin&per_page=1` → `total_count: 2960`
  (complete, not truncated).
- Spot-check of hits (`gh search code --extension fin --json path,repository`):
  all sampled hits are **legacy GNU Fortran testsuite files**
  (`gcc/f/str-io.fin`, `str-op.fin` in gcc forks, FEHM, openuh) — pre-existing
  non-Fin use of the `.fin` extension, plus a dotfiles prompt script.
  **Zero sampled repos use `.fin` for the Fin language.**
- Collision risk: `.fin` already means "preprocessed Fortran (g77 era)" in the
  wild. The future Linguist PR must address this (heuristics / predominant
  usage), or consider that reviewers may flag the collision. Recorded here so
  the PR author doesn't discover it mid-review.
- In-repo corpus: 46 `tests/samples/*.fin` (normative per ADR 0008).
- Grammar drafts (NOT in this repo — Linguist wants standalone grammar repos):
  `/tmp/linguist/` on the LG1 machine: `fin.tmLanguage.json`
  (`scopeName: source.fin`), `language-configuration.json`, `package.json`
  draft for `M1778/fin-vscode`, `languages.yml.snippet`, `fin-keywords.json`
  extracted from `src/lexer/lexer.l` by `extract_keywords.py`, `validate.js`
  (ALL GREEN on 46 samples). Next step: create `M1778/fin-vscode` and paste
  the grammar there for review in #50 before publishing.
- Repo-side stopgap (merged with this change): `.gitattributes` maps
  `*.fin` → `linguist-language=C++` (closest: struct/enum/extern/operator/
  namespace/`::`), keeping `text eol=lf`. Verified with `git check-attr`.
- Fence audit (task 0b): 72 ` ```fin ` fences in README/agent-guide/guide.
  `tests/tools/check_docs.py` regex-matches `^```fin` and **compiles every
  block** — renaming to `cpp` would silently drop doc coverage. Decision:
  leave fences as `fin` (grey on GitHub, but tested). No change made.

## How to update this file

Weekly: re-run the `gh api search/code` query, record `total_count`, and
count Fin-language repos among the top hits (`--jq` on `repository.nameWithOwner`,
eyeball for Fin syntax). Also record VS Marketplace / Open VSX installs once
`fin-vscode` publishes.

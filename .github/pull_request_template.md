## Scope

- What changes, and why:
- ADR / ruling (if behavior changes):

## Verification

- [ ] `cmake --build build` + `ctest --test-dir build --output-on-failure` green
- [ ] Backend present (`FIN_WITH_LLVM=ON`; no `_Codegen` suite going quiet)
- [ ] Bootstrap fixed point: `finc_stage2` built, `cmp finc_stage2 finc_stage3` identical
- [ ] Stage agreement suite ran (not skipped) wherever the stage links
- [ ] `tests/tools/check_docs.py --finc <binary>` green (if docs/examples touched)

## Risk

- [ ] No gate weakened: no `continue-on-error`, no `|| true`, no new skip without an owner
- [ ] No silent drop: refused constructs diagnose (backend invariant holds)
- [ ] CLI changed? `docs/finc-interface-contract.md` updated
- [ ] `git status` shows only intended files; no commits amended

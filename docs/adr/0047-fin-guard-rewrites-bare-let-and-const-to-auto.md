# Bare `let`/`const` rewrites to `<auto>` with a warning

## Context

Fin requires every `let`/`const` declaration to carry a type annotation (or a
nullable `?` marker). A bare `let c = expr` / `const c = expr` -- no
annotation, no `?` -- was a hard error, which punished the most natural
spelling of "infer this" while an explicit `let c <auto> = expr` already meant
exactly that and compiled silently.

## Decision

fin-guard (on by default): a bare `let`/`const <name> = ...` is accepted and
rewritten to `<name> <auto> = ...` with one warning per site (exit 0).
`--no-fin-guard` rejects it instead (exit 1). A written `<auto>` is never a
rewrite and stays silent.

The mechanism is a flag, not a shape: the parser spells the type as `<auto>`
and sets `finGuardRewritten` (src/parser/parser.y:680-697 for declarations,
parser.y:1950-1967 for the for-loop header forms, which exist so the two
spellings cannot disagree; the marker field is
src/ast/stmts/VariableDecl.hpp:20-24). Semantics acts on the flag in
src/semantics/impl/Analyzer_Decl.cpp:17-38: error naming the written keyword
(`is_mutable` tells `let` from `const`) when the guard is off
(Analyzer_Decl.cpp:26), warning verbatim
`fin-guard rewrote '<kw> <name> = ...' as '<kw> <name> <auto> = ...' (pass
--no-fin-guard to reject this instead)` and clearing the flag when on
(Analyzer_Decl.cpp:35-37). The toggle threads from `CompilerOptions::finGuard`
(src/driver/CompilerOptions.hpp:47-49, default `true`) through
`SemanticAnalyzer::setFinGuard`
(src/semantics/SemanticAnalyzer.hpp:55-56, field at 208-210), wired by the
driver (src/driver/Driver.cpp:228) and advertised in `--help`
(src/main.cpp:30).

The stage compiler walks the same path: bare declarations arrive with an
empty type marker and take the same warn-or-refuse branch
(finc/checker.fin:6785-6813, top-level form at 9280-9297, flag at 603-605),
its parser marks the same shape (finc/parser.fin:1951), its driver threads the
flag (finc/driver.fin:2478, 2711) and prints the same help line
(finc/driver.fin:2405; finc/main.fin:41, 94-96).

## Considered Options

- **Reject always:** the pre-guard status quo. Keeps the grammar honest at
  the cost of rejecting a spelling the language can already express -- bare
  `let` means nothing `<auto>` does not.
- **Infer silently:** accepts the spelling but hides that it did. The
  annotation requirement exists so types stay readable; a silent rewrite
  would erode it without anyone noticing.
- **Warn-and-rewrite (chosen):** the program compiles, and the diagnostic
  teaches the spelled form plus the opt-out in one line. Warnings never fail
  the build (the `warnOnHostBranch` precedent), so exit 0 holds.
- **`--no-fin-guard` as the default:** rejected. The guard is the migration
  path toward explicit types, not a gate existing code must opt out of to
  keep building.

## Consequences

- Pin: `FinGuard.BareLetRewritesToAutoWithWarningByDefault`
  (tests/test_cli.cpp:3060) and its `const` twin (3090) assert exit 0 plus
  exactly one verbatim warning per site; `BareLetIsRejectedWhenGuardIsOff`
  (3071) and `BareConstIsRejectedWhenGuardIsOff` (3101) assert exit 1 and the
  `needs a type annotation or '?' (fin-guard is off)` error under
  `--no-fin-guard`; `ExplicitAutoStaysSilent` (3080) and
  `ExplicitConstAutoStaysSilent` (3110) prove the flag (not the `<auto>`
  spelling) is what warns; `FlagIsAdvertisedInHelp` (3120) pins the help
  text.
- The `const` spelling took the same rewrite path from the start, so the
  guard cannot disagree by keyword; `let d;` (no initializer at all) is
  untouched and still an error.

# A module body calls its own module's function, whatever the import order

## Context

Two stdlib modules publish one free-function name with different signatures
(`join` in `strings::std` vs `path::std`, `to_chars` in both). A module body
calling that bare name -- `replace` in strings calling `join` on its split
parts -- must reach its own module's definition regardless of which import
comes first. It did not: the first module in load order silently answered
every module's calls (path-first order ran path's `join` on strings' parts),
and a root call to a duplicated generic instantiated the first-loaded
module's template while the analyzer had bound the last import -- one import
order silently ran the wrong body.

## Decision

Owner-first, both compilers; import order never decides what a module body
calls.

- **Stage:** the driver appends whole module files, so pass 1 keeps the first
  bodied spelling and renames later duplicates to `<name>__dup<N>`
  (`dup_claim_name`, finc/checker.fin:1031-1089; claimed at 8923-8938, bounds
  at 610-620: 64 aliases, 2048 seen names, older first-wins past that). Bare
  calls rewrite to the nearest same-origin definition measured from the body
  being checked (`dup_resolve`, finc/checker.fin:1118-1146; applied at
  5946-5952, skipping method/scoped/module calls and struct-prefixed names).
  With each module chunk appended atomically, nearest IS the caller's own
  module -- the same answer the C++ compiler gets by checking each module in
  its own scope (src/utils/ModuleLoader.cpp:322-326). Bodiless declarations
  (`@define` externs) never claim and never compete (checker.fin:8929-8932,
  1128-1130): two externs of one name publish one C symbol first-wins, like
  `retainAmbientPrototype` (ModuleLoader.cpp:354-375), and renaming one
  would orphan its callers at link time.
- **C++ backend:** each queued body remembers its owning unit (`ownerOf`,
  CodeGen_LLVM.cpp:3512; recorded at drain at 4222-4225 and for template
  instantiation at 7339-7354). A call from an owned body declares its
  owner's declaration under a module-qualified key (`moduleKeyOf`,
  CodeGen_LLVM.cpp:3551, `mod.<i>.<name>` -- dotted, which no Fin identifier
  can collide with) instead of reusing the first-in-load-order entry
  (`ensureOwnerCallable`, CodeGen_LLVM.cpp:3572; called before the
  unqualified lookup at 11516-11525, so even a root-hit entry cannot
  short-circuit it). The unqualified entry is reused only when its recorded
  declaration is already the owner's (`FnInfo::source`, set at 5620), so
  recursion and root calls share one body. A `#[llvm_name]` declaration
  keeps its C symbol -- the link contract wins over the qualified key.
- **Duplicate generics:** the analyzer records which loaded module each
  imported name came from (`importedOwner_`,
  src/semantics/SemanticAnalyzer.hpp:661; named imports overwrite, star
  imports bind only what the scope lacks, a file-scope `fun` erases:
  src/semantics/impl/Analyzer_Decl.cpp:1215, 1235) and stamps it on a still-
  generic call (`resolved_generic_owner`,
  src/semantics/impl/Analyzer_Expr.cpp:1976-1989). The backend instantiates
  THAT module's template under its module-qualified key -- after the owner
  path (a module body's call to its own generic wins first, via
  `ownerFnTemplate` at CodeGen_LLVM.cpp:11178-11192), before ordinary
  load-order lookup (11204+). A type-divergent duplicate still refuses loudly
  at the call, checked against the resolved module's signature.

## Considered Options

- **First-in-load-order everywhere (the old rule):** simple, and correct for
  externs (one C symbol, first wins). Wrong for bodied definitions: it made
  import order semantically significant with no diagnostic, printing garbage
  in one order and the right answer in the other.
- **Refuse all duplicates:** loud instead of wrong, but `strings` and `path`
  legitimately both publish `join`/`to_chars`, and every importer would pay
  for a collision it did not create.
- **Last-import-wins for module bodies:** fixes the root-call direction while
  leaving module bodies order-dependent -- the exact bug in the other
  direction.
- **Owner-first (chosen):** matches the mental model (a module means its own
  names) and the C++ front end's per-module checking, at the cost of
  qualified keys and per-owner entries in the backend.

## Consequences

- Pins: `Soundness_Modules.
  AModuleBodyCallsItsOwnModulesFunctionWhateverTheImportOrder`
  (tests/test_stdlib.cpp:655) runs `replace("aaa","aa","b")` to `ba` in both
  import orders; `AModuleBodyKeepsItsOwnFunctionWhenTheRootCallsTheOtherOnes`
  (test_stdlib.cpp:674) proves an unqualified hit from a root call cannot
  short-circuit module-local resolution; `Soundness_Codegen.
  ARootCallToADuplicateGenericRunsTheResolvedModulesBody`
  (tests/test_codegen.cpp:7896) runs the last-imported body in each order;
  `ARootCallToDivergentDuplicateGenericsStillRefuses` (test_codegen.cpp:7952)
  refuses `expected 'string', got 'int'` instead of instantiating the wrong
  body; `TwoBodiesEachDeclareTheirOwnFunctionOfOneName`
  (test_codegen.cpp:13567) pins the qualified-key design (`fin.nested.<n>.`
  symbols no program can spell).
- Landed as 502bdbb (both compilers + honest tour text in
  docs/guide/12-standard-library-tour.md) and 6803042 (generic provenance).
  The tour no longer warns that import order matters for `strings`/`path`.

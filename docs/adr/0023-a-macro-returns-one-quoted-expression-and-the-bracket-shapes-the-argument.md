# A macro takes named parameters and returns one quoted expression, and the bracket that delimits a call shapes its argument

A `@macro` declares an ordinary parameter list and a body that returns exactly one `quote`d
expression. Parameters are referenced as `$name`. There are no rule arms, no fragment specifiers and
no repetition. The bracket that delimits a call decides how the arguments arrive: `name!(a, b)` is
two positional arguments, `name![a, b]` is one prototype keyed `0, 1`, and `name!{k => v}` is one
prototype keyed by the written keys. `format!` is not declared as one of these — it is
compiler-implemented, and the corpus is what forces that.

```
@macro magic_add(a, b) {
    return quote { $a + $b; };
}
```

That form is not a proposal in the usual sense: it parses today, expands today, substitutes today,
and crosses a module boundary today. `@macro twice(a) { return quote { $a + $a; }; }` with
`let v <string> = twice!(3);` reports `Type mismatch: expected 'string', got 'int'`, which is only
possible if `3` reached the body. What this ADR mostly does is rule *out* the thing the corpus
sketched, say why, and name the changes that make the sketch's two consumers work anyway — of which
the load-bearing one is a parser change of about twenty lines.

## The corpus declines to specify the form, and its sketch is unbuildable in four independent ways

`tests/samples/macro_definitions.fin` is the only file in the tree that declares an `@macro`, and
all of it sits inside a `/* [WIP] … */` block whose line 8 reads
`// Hygienic Rust-like macro definition (NOT DECIDED YET)`. `tests/samples/macros.fin:5` says the
same thing from the other side — `// Truncated (Rust-Like upcoming...)`. Under ADR 0008 a sample is
authority through its expectations, and `macro_definitions.fin`'s expectation is `//@ ok` over a file
that is one comment. It measures clean because it says nothing. So there is no reading available
here, only a choice, and the rest of this document is careful about which is which.

It is worth being precise about *how* far the sketch is from buildable, because "the syntax is
undecided" understates it. `macro_definitions.fin:13-19` is:

```
($($x:expr),*) => {
    {
        let temp <auto> = new Collection::<int>{};
        $(temp.add($x);)*
        temp
    }
};
```

Four things there do not exist, and only two of them are about macros:

- **Fragment specifiers.** `$x:expr` has no representation. `MacroRule::pattern` is a bare
  `std::string` (`src/ast/decls/MacroDecl.hpp:18`), and `parser.y:1376-1385` fills it from one
  `IDENTIFIER`, one `STRING_LITERAL`, or nothing. There is nowhere to put a fragment kind.
- **Repetition.** `$(...),*` in a pattern and `$(...)*` in a body have no token, no production and
  no node. `#for` and `#index` have lexer rules (`lexer.l:206-207`) and appear in **no** production
  and in **no** `.fin` file in the tree — dead tokens, not evidence.
- **A block expression.** Lines 14-18 are a brace whose value is its last expression, `temp`, written
  with no `return` and no semicolon. Fin has no such form: `let v <int> = { let t <int> = 1; t };`
  is `syntax error, unexpected KW_LET`. That is a grammar question with nothing to do with macros,
  and until it is answered there is no expression for a repetition to expand *into*.
- **The methods it calls.** `temp.add($x)` on line 16 reports `Method 'add' not found in type
  'Collection'` — the shipped type spells it `push` (`lib/std/collection.fin:54`). Line 26's
  `temp.put($key, $val)` is the same: `lib/std/hashmap.fin` declares `__set` and `operator []=`
  (`:68`, `:88`) and no `put`.

Two lines of the sketch do work, and they are worth keeping: `new Collection::<int>{}` on line 15
compiles, and the call spellings on 33-34 parse. Everything structural about it does not. A design
that adopted this text would be committing the language to a block-expression form, a repetition
form, a fragment grammar and two library renames, in order to serve two call sites. That is the case
against it, and it is stronger than "not decided yet".

## The bracket shapes the argument, and that is what removes the need for repetition

The two macros that motivated repetition do not need it, because the corpus already writes what they
should expand to.

`tests/samples/prototype_test.fin:27` and `:30` are normative and compile clean today:

```
let x <HashMap<string, int>> = HashMap::from_prototype({ "a" : 10, "b" : 20 });
let my <Collection<int>>     = Collection::from_prototype({ 0: 10, 1: 20 });
```

Now put `useful_macros.fin:7` and `:12` beside them. `map!{ "alex" => 10, "robot" => 20, }` is
`{ "alex": 10, "robot": 20 }` with a different pair separator, and its declared target on line 7 is
`HashMap<string, int>` — the exact type `from_prototype` returns. `coll![1,2,3,4,5]` wants
`Collection<int>`, and `Collection::from_prototype` takes `{int, T}` (`lib/std/collection.fin:111`) —
integer keys, which is precisely what a bracketed list of five values supplies if the positions are
the keys. `Collection::from_prototype({0: 1, 1: 2, 2: 3, 3: 4, 4: 5})` compiles today. So both
macros are one call each, with no repetition, no block expression and no invented library name:

```
@macro coll(items) { return quote { std.Collection::from_prototype($items); }; }
@macro map(pairs)  { return quote { std.HashMap::from_prototype($pairs); };   }
```

What stands in the way is a deliberate, commented decision in the parser that must be reversed.
`parser.y:2844-2859` flattens `k: v` and `k => v` in macro-argument position into two positional
arguments each, and its own comment justifies this: "The pair is flattened into two arguments … so a
macro body reads `$0`/`$1` for the first pair either way and no argument is dropped."

Both halves of that are wrong. `$0` is `syntax error, unexpected INTEGER, expecting IDENTIFIER` —
`parser.y:2722` is `DOLLAR IDENTIFIER`, and `SubstitutionVisitor::visit(Identifier&)`
(`SubstitutionVisitor.cpp:33-41`) keys on parameter names, of which `0` can never be one. The
spelling the comment offers as the reason does not exist. And while no *argument* is dropped, the
*pairing* is: `map!{ "alex" => 10, "robot" => 20 }` arrives as four expressions of two different
types, from which no body without repetition can rebuild two pairs, and whose array form is not a
`[T]` for any single `T`. The flattening preserved the leaves and discarded the structure that was
the whole content of the call.

So: a braced or bracketed macro call builds **one** argument, a `PrototypeLiteral`. A parenthesised
call keeps today's positional behaviour, which `importing.fin:23`'s `macros.magic_add!(10, 20)`
needs. The bracket is not decoration; it is the only thing at a call site that says what shape the
arguments have, and the corpus uses all three deliberately — `useful_macros.fin`'s own comments say
"dict like macro", "List like macro", "funccall like macro" on lines 10, 12 and 14.

## One rule, not arms, and the sketch's only multi-arm case is one a vararg already covers

`macro_definitions.fin:10-19` gives `my_vec` two arms: a nullary one and a variadic one. That is the
corpus's only witness for arm dispatch, and a vararg parameter subsumes it — `@macro many(a...)`
accepts zero or more arguments today (`test_macro_expander.cpp:93`, and measured against an annotated
target: `many!()` and `many!(1,2,3)` both exit 0). The one caveat is not about varargs: `let v <auto>
= many!()` reports `Empty array literal cannot infer type`, and `let a <[int]> = [];` exits 0, so that
is an inference gap at `auto` over an empty literal, reachable without any macro. So the one thing
arms are shown doing is already done without them.

Arms would also have to be built from nothing: as noted above, `MacroRule::pattern` cannot hold a
pattern. And the half-built arms grammar is not merely idle, it is a crash. `macro name { (x) => { … } }`
parses via `parser.y:1365`, and `MacroDeclaration`'s rules constructor
(`src/ast/decls/MacroDecl.cpp:9-10`) never sets `body`, so `MacroExpander::visit(MacroInvocation&)`
dereferences a null `body` at `src/macros/expander/ExpanderExprs.cpp:62`. Measured: **exit 139**, a
segmentation fault, on a four-line program. Declared-and-never-called is fine (exit 0), which places
it exactly on the invocation path. That is the worst possible reading of "refuse, never skip" — not a
silent drop but a compiler crash — and it is live at HEAD.

The `@` is also not optional, and today it disagrees with itself. `parser.y:1361` requires
`AT KW_MACRO` for the parameter form and `parser.y:1365` forbids it for the arms form, so
`macro_definitions.fin:9`'s own `@macro my_vec {` matches neither: it reports `syntax error,
unexpected LBRACE, expecting LPAREN`. `@` is how every other Fin declaration-modifier is spelled
(`@define`, `@special`, `@implements`), so `@macro` keeps it and the bare `macro` spelling goes with
the arms form that motivated it.

## `format!` is declared, not written, and three separate facts force it

`format!` is called six times: `stdlib/error.fin:19`, `deeptest2.fin:26`, `:63`, `:79`,
`useful_macros.fin:14`, `stdlib/stdio.fin:36`. `lib/std/error.fin:9` records in prose that no
formatting macro exists. It cannot be a macro of the form above, for three reasons that are
independent of each other.

**The format string is a runtime value at one site.** `stdlib/stdio.fin:36` is
`let formatted_str <string> = format!(fmt, ...objects);` inside
`pub fun printf<X: Any<Printable>>(fmt: string, ...objects: [X])` on line 35. `fmt` is a parameter.
Rust's `format!` requires a literal and rejects exactly this; the corpus writes it anyway, in the
standard library, as the definition of `printf`. Whatever `format!` is, it cannot read its format
string at compile time.

**Expansion happens before any type is known.** `Driver.cpp:174-176` runs `MacroExpander` at step
3.5; `SemanticAnalyzer` is step 4 at `:185-191`. A macro therefore cannot see an argument's type. But
`{}` has to be type-directed: `deeptest2.fin:26` passes `cast<&auto>(base_val)` — a pointer into a
generic — alongside an `int`; `:63` passes a `string` and an `int`; `stdlib/error.fin:19` passes a
`string`. Substitution cannot choose a conversion per argument, so no macro under this form can be
`format!` however the pattern language is spelled.

**There is no conversion contract to expand into.** No primitive has a `to_string`, and the corpus
names the interface two different ways: `deeptest2.fin:10` is
`interface Printable { pub fun to_string() <string>; }` and `stdlib/stdio.fin:19` is
`pub interface Printable { pub fun format_str() <string>; }`. A macro body must spell one, and would
then be wrong in the other file — the two files that between them hold four of the six call sites.

So `format!` is implemented by the compiler. It is *declared* in the standard library, in the spelling
the corpus already uses for a declaration whose implementation is elsewhere:

```
@define format!(fmt: string, ...) <string>;
```

`@define printf(fmt: string, ...) <noret>;` is written verbatim at `macros.fin:3`,
`stdlib/stdio.fin:12` and `deeptest2.fin:3`, and reaches a C implementation through
`#[llvm_name="c_printf"]`. `format!` reaches a compiler implementation the same way, and needs no
attribute to say so: a macro has no linker symbol, so there is no other possible implementer. A
`@define`d macro whose name the compiler does not implement is refused **at the declaration** — which
is what keeps this from becoming a name that expands to nothing.

Visibility is the one thing this cannot settle alone, and it should go to the owner rather than be
decided here. `deeptest2.fin` has no imports at all and `stdlib/error.fin` has none either, so both
call `format!` with nothing brought into scope. Two readings:

- The macro namespace is not the symbol namespace. `Scope` keeps them apart — `defineMacro`/
  `resolveMacro` at `Scope.hpp:32,46` against `define`/`resolve` — and the prelude ruling of
  2026-08-27 (owed as ADR 0021, not yet written) ruled on the symbol namespace when it ruled that
  ambience does not exist. A compiler-implemented macro is ambient in the macro namespace by
  construction, the way `cast`, `sizeof` and `new` are ambient in the expression grammar. Six sites
  clear with no sample edit.
- Or that ruling applies as written, `format!` is `#[global]` in `lib/std/stdio.fin`, and that
  contradicts its "`printf` alone is to carry it".
- Or neither, and `deeptest2.fin`, `stdlib/error.fin` and `useful_macros.fin` each owe an import they
  do not write — three sample repairs, in the samples lane, under ADR 0008.

This ADR recommends the first and flags it as the weakest joint in the design, because it puts the
signature in two places: the compiler's table and `lib/std/stdio.fin`. That is the second-place-the-
truth-lives failure ADR 0008 rejected for exception lists. The mitigation is the one the tree already
uses for exactly this shape — `Soundness_Codegen.TheNoBackendHelpNamesThePinnedLlvmMajor` reads the
major out of `CMakeLists.txt` and the string out of `CodeGen_Stub.cpp` and asserts they agree — and a
test that reads both `format!` signatures and compares them is the same move.

## A macro exports through its module, and that already works — but a named import silently does not

`importing.fin:7` claims a quoted file import "imports macros as `macros` by default so it can be
used like `macros.magic_add!(...)`", and line 23 calls it. Both halves of the claim are already true.
`MacroExpander::visit(ImportModule&)` builds a `NamespaceType` aliased to the file stem
(`ExpanderDecls.cpp:34-45`), and `resolveMacro` splits a dotted name and looks the tail up in that
namespace's scope (`ExpanderExprs.cpp:24-35`). Measured end to end: a file declaring
`@macro magic_add(a, b)`, imported as `import "sub/mymacros.fin";` and called as
`mymacros.magic_add!(10, 20)`, exits 0. So the only reason `importing.fin:23` fails is that
`tests/samples/macros.fin` declares no such macro. There is no export machinery owed here.

A macro carries no visibility marker: `pub @macro` is `syntax error, unexpected AT`. A macro is
exported by being declared in a module, and nothing narrower is expressible. That answers
`docs/plan.md`'s "should a named import or `import *` carry macros?" halfway and exposes a defect on
the other half. `import { magic_add } from "sub/mymacros.fin";` reports
`Module 'sub/mymacros.fin' does not export 'magic_add'` — even though `ExpanderDecls.cpp:25-32`
binds it. The expander says yes, the analyzer's export check says no, and the analyzer wins. The
ruling: a named import carries a macro, because the expander already does the work and there is no
argument for the asymmetry. It is one check consulting one more table.

## Hygiene: ADR 0020's rule extends here, and its cost argument does not

ADR 0020 governs code a `@special` handler *injects*: bindings use `compiler.code.fresh()`, and
`compiler.code.ident(name)` takes module-qualified paths only, so injected code cannot capture a
caller's local. A `@macro` body is a different door into the same room, and today it is wide open.
A macro body's identifiers are spelled literally and resolve at the **call site**: with
`@macro twice(a) { return quote { a + a; }; }` — bare `a`, no `$` — the diagnostic is
`Undefined variable 'a'` reported at the *macro body's* line, which is only possible if the body's
names were resolved in the caller's scope and looked up there. A library macro whose body said `temp`
would collide with a caller's `temp`; one that read `count` would bind whatever `count` the call site
happened to have. That is ADR 0020's C-preprocessor failure, arriving unmodified.

Two rules close it, and the design of the form makes both nearly free.

**A macro body may spell only `$param` unquotes, literals, operators, and module-qualified paths.** A
bare unqualified identifier in a macro body is refused at the declaration. This is ADR 0020's `ident`
restriction transposed word for word, and it costs almost nothing here because the body is a single
expression and so cannot declare a binding at all — there is no `let temp` to collide. The
single-expression restriction and the hygiene rule hold each other up, which is an argument for both.

**A macro body's non-`$` names resolve in the declaring module, not at the call site.** This is where
ADR 0020 needs amending rather than merely citing, and the amendment should be argued rather than
assumed. ADR 0020 declined definition-site resolution — the substantive half of Rust's mixed-site
hygiene — on cost: "ADR 0006's interpreter is a tree-walker over the host AST and has no such
machinery." That is true of injected code, where the injection point and the handler are in different
compilations. It is not true here. `ExpanderDecls.cpp:21` already calls `loader->loadModule` and
already holds the declaring module's `Scope`; resolving a macro body's names against the scope of the
module that declared it is carrying a pointer the expander is already given. The cost argument does
not transfer, so the conclusion should not either.

It is also load-bearing rather than decorative, which is what settles it. `coll!` and `map!` as
written above spell `Collection::from_prototype` and `HashMap::from_prototype`. Under call-site
resolution those work in `useful_macros.fin` only because lines 3-4 of that file happen to import
both types — a library macro that works by luck of its caller's imports. Under declaring-module
resolution they work because `lib/std/collection.fin` imported what it needed. ADR 0020's own
`ident`-takes-qualified-paths rule presumes a namespace those paths resolve in; this names which one.

## What is read and what is invented

The most important section, because the corpus explicitly declines to specify this construct. Three
categories, not two: the third is for facts measured off the implementation, which are evidence about
what is cheap but are not specification and could be changed by whoever owns the file.

**Read off the corpus.**

| Decision | Witness |
| --- | --- |
| A macro call is spelled `name!` | `useful_macros.fin:7,12,14`; `importing.fin:23`; `macro_definitions.fin:33,34` (and `macros2.fin:7,11`, though both are inside a comment block) |
| Three call brackets, all three used | `useful_macros.fin:14` `(...)`, `:12` `[...]`, `:7` `{...}` |
| `!{...}` is key-to-value | `useful_macros.fin:7-9`; `macro_definitions.fin:23,34` |
| A qualified macro path crosses a module boundary, aliased by file stem | `importing.fin:7,23` |
| `coll!`'s target is `Collection<int>`, `map!`'s is `HashMap<string, int>` | `useful_macros.fin:12,7` |
| `from_prototype` builds both from a prototype | `prototype_test.fin:27,30`; `lib/std/collection.fin:111`; `lib/std/hashmap.fin:95` |
| A `Collection` prototype is keyed by `int` | `lib/std/collection.fin:111` `{int, T}`; `prototype_test.fin:30` |
| `format!` returns `string` | `stdlib/error.fin:18-19`; `useful_macros.fin:14`; `stdlib/stdio.fin:36` |
| `format!`'s format string may be a runtime value | `stdlib/stdio.fin:36` |
| `format!` is variadic, one to three trailing arguments | the six sites |
| `format!` is called with no import | `deeptest2.fin` and `stdlib/error.fin` have no imports |
| `@define` means "implementation is elsewhere" | `macros.fin:3`; `stdlib/stdio.fin:11-12`; `deeptest2.fin:3-4` |
| The `@macro` form is undecided | `macro_definitions.fin:8`; `macros.fin:5` |

**Invented — chosen here, with no corpus witness.**

| Decision | Note |
| --- | --- |
| One rule per macro, no arms | The sketch shows two arms (`macro_definitions.fin:10-19`); a vararg subsumes that case |
| Named parameters, `$name` unquote, `quote` body | `quote` appears in **no** `.fin` file in the tree; this is the implemented form, not a read one |
| One expression per macro; no statements, no block value | Rules out `macro_definitions.fin:14-18` |
| `![...]` and `!{...}` collapse to one prototype argument | Reverses `parser.y:2850-2853` |
| A bracketed list's keys are `0..n-1` | Analogous to `prototype_test.fin:30`, not written there |
| `@define name!(...) <T>;` for a compiler-implemented macro | Extends `@define`; the `!` in that position is new |
| `format!` is compiler-implemented and ambient in the macro namespace | The visibility half is flagged above as an owner question |
| A macro body's names resolve in the declaring module | Amends ADR 0020 |
| A bare unqualified identifier in a macro body is refused | Transposed from ADR 0020 |
| A named import carries a macro | Today it does not; the two passes disagree |
| No visibility marker on a macro | `pub @macro` does not parse; forced, but still a choice |
| `from_prototype`, not the sketch's `add`/`put` | The sketch's two method names do not exist |

No name outside the tree is introduced. `format`, `map`, `coll`, `magic_add`, `from_prototype`,
`Collection`, `HashMap`, `quote`, `@define`, `@macro` are all written in the corpus or the shipped
library. Nothing here needs `Offer`, `PathLike`, `Strict`, `Struct`, `pointer_type`,
`_static_string`, `Alloc`, `Free`, `rm` or `defined`, and none of them appears above.

**Measured off the implementation — evidence, not specification.** Expansion precedes analysis
(`Driver.cpp:176` vs `:185`). The arms form crashes (exit 139). `$0` does not parse. A block
expression does not parse. `pub @macro` does not parse. `Collection::add` does not exist. A
whole-module macro import works; a named one is refused. Substitution works when the body writes
`$a`, which is why `test_macro_expander.cpp:110`'s recorded defect is a misdiagnosis: that test
writes bare `a` and concludes "the argument is not substituted", when what it measured is that the
form requires the sigil.

## Implementation plan

Nine steps. Steps 1-6 are the front-end/semantics lane, 7 is codegen's, 8-9 are the samples lane, and
each is verifiable on its own.

1. **Refuse the arms form.** `src/macros/expander/ExpanderExprs.cpp:60-76`: if `is_rust_style`, report
   at the declaration and return, before the null `body` is touched. Better, delete `parser.y:1365`
   and the `macro_rules`/`macro_rule` nonterminals and the `MacroRule` field with them, so
   `macro name { … }` is a syntax error rather than a refused construct. *Verified by:* the four-line
   program in this ADR exits 1, not 139. This is independent of everything else and should land
   first, because a crash at HEAD outranks a design.
2. **The `@` becomes mandatory.** `parser.y:1361` is the only macro-declaration production left after
   step 1. *Verified by:* `macro f(a) { … }` is a syntax error; `@macro f(a) { … }` is not.
3. **Bracket shaping.** Replace the flattening at `parser.y:2838-2859`. `macro_arg_item` keeps
   `expression` for the parenthesised form; the braced and bracketed forms build one
   `PrototypeLiteral` — written keys for `{...}`, integer literals `0..n-1` for `[...]`. Note that
   `macro_arguments` accepts a trailing comma (`:2872`) and a prototype literal does not
   (`{"a": 1,}` is `syntax error, unexpected RBRACE`), so the node must be built directly, which a
   parser action does anyway; `useful_macros.fin:9` has that trailing comma. *Verified by:* an
   `@macro m(p)` whose body is `$p` called as `m![1,2,3]` type-checks against `<{int, int}>`, and as
   `m!{"a" => 1}` against `<{string, int}>`.
4. **Declaring-module resolution and the hygiene refusal.** `src/macros/**`: store the declaring
   module's `Scope` on the `MacroDeclaration` when `ExpanderDecls.cpp:17-46` loads it, resolve a
   body's non-`$` names against it, and refuse a body's bare unqualified identifier at the
   declaration. *Verified by:* a macro in module A whose body names a type A imports expands in a
   caller that does not import it; and `@macro f(a) { return quote { tmp + $a; }; }` is refused with
   no call site present.
5. **A bodyless macro declaration.** `parser.y`: `@define IDENTIFIER NOT LPAREN … RPAREN LT type GT
   SEMICOLON`. *Verified by:* `@define format!(fmt: string, ...) <string>;` parses and
   `--debug-ast` shows a `MacroDeclaration` with no body.
6. **The builtin macro table.** `src/semantics/**`: one table of compiler-implemented macro names and
   signatures; `format` is its only entry. A `@define`d macro not in it is refused at the
   declaration. `format!` resolves with no import (see the visibility question above — this step
   should not land until that is ruled). Arity and first-argument type are checked here, in the
   analyzer, because that is the first pass that knows a type. Also: the analyzer's import-export
   check must consult the macro namespace, so a named import of a macro stops being refused.
   *Verified by:* `format!()` is refused for arity; `format!(1, 2)` is refused for a non-`string`
   first argument; `format!("{}", x)` type-checks as `<string>`; and the five `Undefined macro
   'format!'` diagnostics in `deeptest2.fin`, `stdlib/error.fin` and `useful_macros.fin` are gone.
7. **Lowering.** `src/codegen/**`: `format!` becomes a runtime call that builds a `string` from a
   format string and N values, dispatching per argument on the type the analyzer recorded. *Verified
   by:* a `-o` build of a `format!` call runs and prints. This is the only step with a runtime
   obligation and the only one that can be deferred without leaving a diagnostic behind — a checker-
   only build is green after step 6.
8. **Declarations in the library and the corpus.** `lib/std/stdio.fin` gains the `@define format!`
   line; `lib/std/collection.fin` and `lib/std/hashmap.fin` gain `coll!` and `map!`;
   `tests/samples/macros.fin` gains `@macro magic_add(a, b)`. *Verified by:* `importing.fin` reports
   nothing at 23:20.
9. **Expectations.** `useful_macros.fin`, `stdlib/error.fin` and `deeptest2.fin` flip from
   `unimplemented` to `ok`; `stdlib/stdio.fin`'s note drops `format!` and goes from 13 diagnostics to
   12; `importing.fin` flips if step 8 lands. *Verified by:* the corpus harness, and by hand-summed
   counts rather than a pipeline's exit code.

Measured at HEAD `84f914c`, so the plan's arithmetic is checkable: `useful_macros.fin` 3 errors,
`stdlib/error.fin` 1, `deeptest2.fin` 3 — seven diagnostics and three samples that go clean — plus
one of `stdlib/stdio.fin`'s 13. Eight diagnostics, three samples, which is what the ruling predicted.
`importing.fin`'s single diagnostic is a ninth and is **not** in that eight: clearing it needs an edit
to `tests/samples/macros.fin`, which is the samples lane's file, and note that adding a macro there
makes that file's line 5 comment, "Truncated (Rust-Like upcoming...)", false.

## Tests

New, as `Soundness_Macros.*`: the arms form is a syntax error rather than a crash; a bare `macro`
without `@` is a syntax error; `m![...]` and `m!{...}` each arrive as one prototype; a macro body
resolves a name its own module imported and its caller did not; a bare unqualified identifier in a
body is refused at the declaration; `format!` is refused for arity and for a non-`string` first
argument and types as `<string>`; a named import carries a macro.

`test_macro_expander.cpp:110` `SubstitutesTheArgumentIntoTheExpansion` must be rewritten, not
deleted. Its comment records a substitution defect that does not exist — it writes bare `a` where the
form requires `$a` — so the fix is to correct the diagnosis in the comment and tighten the body to
the positive assertion the test itself says to make ("tighten this test"). It is not named
`KnownDefect_`, so nothing inverts formally; the correction is owed anyway, because a test that
misnames what it measures is worse than one that fails.

No existing `KnownDefect_` test is about macros, so nothing inverts.
`Soundness_ArrayExtent.AnExtentSurvivesAPreprocessorDefine` (`test_soundness.cpp:8810`) says "the
`@macro` form has no expression body yet" — still true under this design, and it stays: see below.

## What this does not solve

**`macro_definitions.fin` stays commented out.** It measures `//@ ok` and OBJECT_CLEAN today because
it is one comment, and this design does not make its text compile — it rules that text out. If the
samples lane un-comments it, the file breaks on all four of the missing features named above. It
should keep its `/* [WIP] */` block, and its line-8 comment should be updated from "NOT DECIDED YET"
to a pointer at this ADR, so a reader learns the form was decided *against*.

**`coll!` and `map!` will produce empty containers.** `lib/std/collection.fin:106-110` and
`lib/std/hashmap.fin:92-94` both say `from_prototype` "cannot read the prototype yet … the keys are
dropped". So `useful_macros.fin` goes green over calls that type-check and compute nothing. Under
ADR 0008 that is a legitimate `ok` — the expectation is about diagnostics — but it is a green sample
over a library that discards its input, and it should be recorded in the sample's note rather than
discovered later. Walking a prototype is the blocker, and it is not a macro problem.

**Macros in enum and type position stay unbuilt.** `macros2.fin:7` writes `BufferSize = GET_SIZE!()`
in an enum body and `:11` writes `data <[int, CONFIG_MAX!()]>` in a type extent. Both are inside a
comment block. The form here expands in expression and statement position only; nothing about it
forecloses the other two, but nothing about it delivers them either, and `test_soundness.cpp:8810`
notes that `#cdef` is what works in an extent today.

**No repetition, ever, is not what this says.** It says the two macros in the corpus do not need it,
and that building it would cost a fragment grammar, a repetition grammar and a block-expression form.
If a later consumer genuinely needs one body element per argument, that consumer is the evidence and
this ADR should be amended against it. What must not happen is repetition arriving as grammar with no
substitution behind it — `docs/plan.md:1343-1344` already names that failure once, and the arms form
crashing at HEAD is what it looks like.

**The prototype trailing-comma inconsistency stays.** `{"a": 1,}` is a syntax error while
`m!{"a" => 1,}` is not, after step 3 as before it. Step 3 works around it by building the node
directly. Whether a prototype literal should accept a trailing comma is a grammar question with no
corpus site asking it.

**An empty collection literal has no expansion target.** `coll![]` would shape to `{}`, and an empty
prototype literal is `syntax error, unexpected RBRACE`. `macro_definitions.fin:10-12`'s nullary arm
returned `new Collection::<int>{}` for exactly this case, so the sketch had an answer here and this
design does not. No corpus site writes `coll![]` or `map!{}`, so nothing is regressed and nothing is
owed yet — but the day one is written, the fix is either an empty-prototype production or a nullary
special case, and it should be recorded as a gap rather than discovered as a syntax error inside an
expansion. Note the failure mode is honest either way: a syntax error at the call, not a macro that
expands to nothing.

**Two things go to the owner.** `format!`'s visibility, argued above — ambient in the macro
namespace, or `#[global]` against the prelude ruling's "`printf` alone", or three sample repairs. And
whether `@define` on a macro is the right spelling for "the compiler implements this", or whether that
deserves a marker of its own; this ADR takes the first because it invents no attribute, and the cost
is the two-places-the-truth-lives risk named above. The first of the two is now ruled — ADR 0021
took it, and "Step 6 as landed" below records the mechanism; the second still stands.

## Step 6 as landed

The visibility question the plan defers is settled, and settled in ADR 0021 rather than here: a
compiler builtin resolves bare because it is in **no scope**, so `resolveMacro` failing is not an
error for a table name. Ambient publication was the alternative and is not merely unnecessary but
unimplementable — `publishIfGlobal` runs during analysis and macro invocations are resolved by the
expander, which runs before it, so a macro written into an ambient scope would be written after
the only pass with a use for it. ADR 0021 carries the measurement.

The table is `src/semantics/BuiltinMacros.{hpp,cpp}`, one row, in `CompilerApi.hpp`'s style: data
the analyzer reads, so the second builtin is a row and not a branch. Membership has two
consequences and they are checked in two places. The **expander** waves a table name through — a
failed lookup returns silently instead of reporting `Undefined macro` — and also waves through
*every* bodyless declaration, because a bodyless declaration is a claim about who implements the
macro and this pass is not the implementer. The **analyzer** answers the invocation: arity, then
the fixed parameters' types, then `lastExprType` from the declared return. Arity is hand-written
rather than delegated to `checkCallArity`, which skips the count entirely for a variadic
signature.

The declaration's legality is checked at the declaration: `@define frobnicate!(a: int) <int>;`
reports `The compiler implements no macro named 'frobnicate!'` with no call site present, and the
help row lists the signatures the compiler does implement. One diagnostic at the bad declaration
beats one there and one at every call. A bodied `@macro format(a) { ... }` of a builtin's name
still expands, because the table is consulted only after `resolveMacro` — a builtin name is not a
reserved word.

The signature is **not** compared against the table's. Step 5's parser keeps a `MacroParam` of a
name and a fragment kind and drops the declared parameter types, so a check could compare arity,
vararg-ness, names and return type — three of a signature's four parts — and would read as a
check over all four. Measured: `@define format!(fmt: int) <int>;` is accepted, and a call still
types as `<string>` from the table. The mitigation this ADR names is a text-comparison test, and
it belongs with step 8, which is what writes the line into `lib/std/stdio.fin`.

The import-export clause needed no work: `Analyzer_Decl.cpp:929-932` already consults
`moduleScope->resolveMacro` on a named import, landed with `b2870c0` (*A named import carries a
macro*) before this step was reached. Its comment there is worth reading beside this step, because
it records the same pass-ordering fact from the other side: the analyzer defines the macro into
its own scope's macro map and then never reads it, "because expansion is a finished pass by the
time this runs".

`SemanticAnalyzer::error` gains a help-carrying overload beside the two-argument one. The existing
located `reportError` fills the `= help:` row from the typo heuristic, and a rule the compiler can
state outright is worth more than a guess at what the programmer meant.

The corpus arithmetic came out as the plan predicted, and step 6 forced three of step 9's edits
early because the samples stopped failing the moment the table landed: `deeptest2.fin` rc=1 n=3 →
rc=0 n=0 and `stdlib/error.fin` rc=1 n=1 → rc=0 n=0, both promoted to `//@ ok`;
`useful_macros.fin` 3 → 2, still `unimplemented` on `map!` and `coll!`, which wait on step 8;
`stdlib/stdio.fin` 12 → 11 and its note drops `format!`. **TOTAL 64 → 58 diagnostics over 51
samples.** The clause's "five" is the count in the three files it names — three, one and one —
and `stdlib/stdio.fin`'s sixth is booked separately in step 9, so six diagnostics leave and the
snapshot moved by exactly six. The arithmetic checks.

Two documents the count falsified are corrected here rather than left: `test_expectations.cpp`'s
tally read "16 ok, 33 unimplemented", which is a count from long enough ago that the two numbers
had swapped ends — measured now at 33 `ok`, 17 `unimplemented`, 1 `error`. And `deeptest2.fin`'s
`//@ ok` is a statement about diagnostics under ADR 0008, not about a build: `finc -c` still
refuses at its line 29.

Seven `Soundness_Macros` tests replace step 5's interim `ABodylessMacroHasNothingToExpandAndSaysSo`:
bare resolution with no declaration of any kind; the return type asserted through a negative
(`let n <int> = format!("hello")` must report `expected 'int', got 'string'`, or a null type would
pass a positive); arity with the signature in the help row; a fixed-argument type refusal beside a
three-of-mixed-types acceptance, which is what pins the variadic tail as unchecked; a bodyless
declaration of an unlisted name refused with no call; a bodyless declaration of a builtin accepted
*and* asserted not to produce the expander's wording, which is the one-mistake-one-diagnostic
property; and a program's own bodied `format` winning. 1695 pass.

Lowering was untouched at that point, so a `format!` call type-checked and `-o` reported
`codegen: a macro invocation (macro expansion did not consume it) is not lowered yet`. That was
step 7, recorded below, and the plan already said a checker-only build is green after step 6.

## Step 7 as landed

`format!` lowers to two calls to C's `snprintf` around one `malloc`: the first measures with a null
buffer, the second writes into the buffer that measurement sized. Measuring rather than guessing a
capacity is the whole reason `snprintf` and not `sprintf` is the runtime this leans on — a fixed
buffer would make the longest value a program can print a property of this file.

The conversion per argument is read off the **promoted** value, after `promoteVararg`, because that
is the value C actually receives: a `float` has already become a `double` and takes `%g`, and a
sub-int integer has already widened. Four of those choices are visible bytes and each is pinned as
one: `%g` rather than `%f`, so `format!("{}", 1.5)` reads `1.5` and not `1.500000`; `%lld` rather
than `%ld`, which agrees with `%ld` on Linux and would disagree on Windows, where C's `long` is 32
bits; signedness taken from the argument's own type rather than the placeholder, since `{}` carries
no type; and a `char` printing as a number, which is not a decision so much as a consequence —
`char` shares its Layout row with `int8`, so there is nothing left at this level to tell them apart.

The placeholder scan happens at compile time, and that is what fixes the shape of the feature. `{}`
is the only placeholder; `%` is literal text and becomes `%%`; a count that disagrees with the
arguments is a compile error rather than a read off the stack; and an aggregate is refused by name
(`a struct formatted by 'format!'`, and likewise an array, an interface reference, a prototype and
a function value) rather than given an invented spelling, because what `{}` means for a user's type
is a language question and not a lowering one. `promoteVararg` would already refuse an aggregate at
the C variadic boundary, but it would call it "a variadic argument"; the refusal here names what the
author wrote.

The one thing the language allows that the lowering does not is a format string that is not a
literal. `lib/std/stdio.fin`'s note records the consequence: a `printf` forwarding its own `fmt` is
exactly the shape that needs a runtime formatter, and it reports `a 'format!' whose format string is
not a literal` instead. Nobody frees the returned buffer either — which is what makes returning one
from a function safe, and what `lib/std/error.fin` now gives as the reason `describe()` keeps its
`snprintf` body: that body hands the caller a buffer to free, and trading a stated ownership rule
for a silent leak is a swap that belongs with ADR 0003's collector.

The names live in one place: codegen reads the same `builtinmacros::find` the analyzer does, so
`visit(MacroInvocation&)` dispatches on the table rather than on a string spelled twice (ADR 0008).
A bodyless declaration of a table name lowers to nothing; a macro with a body still reports `a macro
declaration (macro expansion did not consume it)`, since expansion should have consumed it.

Thirteen `Soundness_Codegen` tests, eight of them executed programs rather than compilations: the
step's own verification clause, a seven-type dispatch in one call, a pointer beside a string, a `%`,
a placeholder-free format string, a result returned out of the function that built it, a `format!`
nested as another's argument, and a bodyless declaration linking and running with no code emitted
for it. 1708 pass, and the corpus snapshot is byte-identical to
step 6's — this step adds no diagnostic to a checker-only build.

## Steps 8 and 9 as landed

Step 8 puts the declarations where the plan said: `@define format!` into `lib/std/stdio.fin`, `@macro
coll(p)` into `lib/std/collection.fin`, `@macro map(p)` into `lib/std/hashmap.fin`, and `@macro
magic_add(a, b)` into `tests/samples/macros.fin`.

`coll!` and `map!` are one-line renames: because step 3 shapes bracketed calls into prototype
literals (`[...]` keyed `0..n-1`, `{...}` with written keys), the argument arrives matching the
`from_prototype` signatures the two collection libraries already declared. Both bodies qualify their
call paths (`Collection::from_prototype`, `HashMap::from_prototype`) because ADR 0020 and step 4
forbid unqualified names in macro quotes. Both carry `from_prototype`'s limit with them: the keys are
dropped because nothing in the language can walk a prototype yet, but the calls type-check as
`Collection<int>` and `HashMap<string, int>`.

In `tests/samples/macros.fin`, adding `@macro magic_add(a, b) { return quote { $a + $b; }; }`
resolves the qualified macro call `macros.magic_add!(10, 20)` in `tests/samples/importing.fin:23:20`.
`macros.fin` line 5's historical comment "Truncated (Rust-Like upcoming...)" is corrected to record
that the Rust-like arms form was refused by step 1, and the file is no longer truncated.

Step 9 settles the corpus expectations:
  * `useful_macros.fin` promotes from `//@ unimplemented` to `//@ ok`. Its two `Undefined macro`
    diagnostics for `map!` and `coll!` are gone. Lines 3-4 are repaired to name `map` and `coll` in
    their imports (`import { HashMap, map }`, `import { Collection, coll }`), following the rule that
    a named import carries only the names it spells. The previous note is preserved verbatim.
  * `stdlib/stdio.fin`'s note is updated: the count was corrected from 13 to 12 (measured before step
    6), and with `format!` resolved in step 6 the count is now 11 diagnostics.
  * `importing.fin`'s note is updated: line 23:20's `macros.magic_add!` is resolved; its remaining
    four diagnostics (`module not found: somelib`, `Failed to load module 'somelib'`) are preserved
    and documented as following the intentional deletion of `lib/std/somelib` in `4d79ae7`.
  * `deeptest2.fin`'s note is updated to record that `format!` now lowers as of step 7.
  * `tests/test_expectations.cpp` reflects the new tally: 34 `//@ ok`, 16 `//@ unimplemented`, 1
    `//@ error`.

The corpus snapshot shows total diagnostics dropping from 58 to 55 across 51 samples. All 1708 ctest
checks pass.


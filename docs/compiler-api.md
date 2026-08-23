# The Fin compiler API

Design of the compile-time compiler API: the **compiler component** surface and the **event**
system. Ratified before implementation, per the project owner's mandate that this be "a strong
compiler API that surpasses any other compiler".

This document is the design. It is not a tutorial and not an implementation. It scopes wave 4 of
`docs/plan.md` and it is written against ADR 0003 (memory management is a library), ADR 0006
(comptime is a tree-walking interpreter) and ADR 0007 (handlers read broadly, write narrowly).

Vocabulary is `CONTEXT.md`'s. Where this document needs a word `CONTEXT.md` does not have, it says
so and proposes one rather than using a synonym for an existing term.

---

## 0. What is actually there — the measured substrate

Everything below was measured in this tree, not inferred. It matters because several items on
`MetaType`'s agreed minimum surface have **no substrate at all**, and that is a wave-4 cost, not a
wiring job.

### 0.1 What already exists and works

| Thing | Where | State |
| --- | --- | --- |
| `quote { ... }` | `lexer.l:129`, `parser.y:1435`, `ast/exprs/Lambda.hpp:12` | Parses. `QuoteExpression` wraps a `Block`. |
| `$name` unquote | `parser.y:1409` | Parses — as `Identifier("$" + name)`. A string hack, not a node. |
| `@special f(p) <T> { }` | `parser.y:882,887` | Parses, with and without attributes. `SpecialDeclaration`. |
| `#[on(variable_scope_exit)]` | `parser.y:385` | **Parses today.** `#[id(dotted_path)]` is an existing production. |
| `#[use(compiler.components.types)]` | `parser.y:385` | Parses today, same production. |
| `compiler.system.get_total_memory(x)` | — | **Parses** today. Fails in the analyzer with `Undefined variable 'compiler'`. |
| Attributes on struct members | `StructDecl.hpp:20` | `StructMember::attributes` exists and parses. |
| Attributes on variable declarations | `VariableDecl.hpp:19` | `VariableDeclaration::attributes` exists. `#[slaveof(z)]` is attachable. |
| Declaration order of struct members | `StructDecl.hpp:28` | `std::vector<StructMember>` — order **is** preserved in the AST. |

### 0.2 What does not exist, measured

| Thing the design needs | Measured state |
| --- | --- |
| Turbofish on a dotted path — `compiler.structs.select_field::<int>(...)` | **Syntax error** at the `::`. `parser.y:1416` accepts turbofish only after a bare `IDENTIFIER`. This is `types.fin:23`, and it is **not in wave 2's list in `docs/plan.md`.** |
| `$struct`, `$interface`, `$enum_member` as types | Only `$type` has a production (`parser.y:961`). `$struct` is `unexpected KW_STRUCT, expecting KW_TYPE`. |
| `$type` as a resolvable analyzer type | `Undefined type '$type'`. Parses, never registered. |
| `@f(...)` as an expression | **Syntax error**, `unexpected AT`. `AT` appears in four grammar places, all declaration headers. So no `@special` can be called at all. |
| Size, alignment, field offset, layout | **Zero occurrences** of `offset`, `getSize`, `alignment` or `layout` in all of `src/types/` and `src/semantics/`. Not partial — absent. |
| Ordered fields on the *semantic* type | `StructType::fields` is `std::unordered_map<std::string, FieldInfo>` (`StructType.hpp:28`). Declaration order is preserved in the AST and **thrown away** by the type. |
| `readonly` | **Zero occurrences** in `lexer.l`, `parser.y`, `src/ast/`, `src/types/`. `CONTEXT.md` ratifies it; the compiler has never heard of it. |
| `const` as a parameter qualifier | `KW_CONST` appears only in `const x <T> = e;` productions. `fun test(const a: int)` from `const.fin:9` cannot parse. |
| Field visibility beyond public/private | `FieldInfo { TypePtr type; bool is_public; }` — no readonly, no default value, no attributes on the *semantic* side. |
| Enum member types | `EnumDeclaration::values` is `vector<pair<string, Expression>>` — no field could hold a type. |
| Attribute machinery of any kind | `Attribute::accept` is empty, no `visit(Attribute&)` exists, only `ASTPrinter` reads them. |

### 0.3 The three consequences that change the design

**`pointer_offsets` has no floor to stand on.** ADR 0003 commits to a tracing collector written in
Fin. A tracing collector needs, for a given `$type`, the byte offsets of the words that hold
pointers. Fin has no byte offsets, no sizes, no alignment, and no ordered field list on the semantic
type. So `compiler.layout.*` is not an API over existing machinery — **the layout pass is part of
wave 4**, and it is the largest single item in it. See Q7 for where layout truth should live.

**`quote` is currently analysed as live code.** `Analyzer_Expr.cpp:541` walks a quote's block with
the ordinary analyzer, so `quote { f($v); }` reports `Undefined function or type 'f'` at the
*definition* site. A quote is data, not code, and this must be inverted before any handler can
return one.

**A constant must live with the component that consumes it.** ADR 0012 makes `#[use(...)]` cover the
whole operations layer, constants included. I had proposed exempting constants; that is **withdrawn**
(§2.1b). The consequence is a positive design rule rather than an exemption: filing a constant away from
its consumer forces two grants for one call, which is how `memory.fin:26,37` came to be wrong. All three
recorded stdlib violations stand, and they are header edits.

---

## 1. Survey: what the bar actually is

"Surpasses any other compiler" is only a claim if you know what the others do. Every fact below was
read from primary documentation during this design, with the URL given. Nothing here is recalled.

The survey is organised around one question, because it is the question Fin's design turns on:

> Can a **library** — code the user merely imports and arms — observe the program's types *with
> layout*, and inject code at a point it does not lexically occupy?

Every system below answers *no* to at least one half of that.

### 1.1 Rust — procedural macros

The whole public surface of `proc_macro` is lexical: `TokenStream`, `TokenTree`, `Group`, `Ident`,
`Punct`, `Literal`, `Span`, `Delimiter`, `Spacing`.
[doc.rust-lang.org/proc_macro](https://doc.rust-lang.org/proc_macro/index.html)
No item in the crate could carry semantic information, because proc macros run *before* name
resolution and type checking. **Cannot:** query a resolved type, a size, an alignment, a field
offset, whether a type implements a trait, or where a name is defined. The lexicality is total —
`Literal`'s own docs note that "Boolean literals like `true` and `false` do not belong here, they are
`Ident`s", so `true` is a token, not a value. The idiomatic workaround is to *emit*
`size_of::<T>()` and let the compiler evaluate it later, which means the macro never learns the answer
and cannot branch on it. There is no hook that fires on a program event. **A garbage collector cannot
be a proc macro.**

### 1.2 C++26 static reflection — P2996

The most complete *introspection* surface surveyed, and no injection at all. One operator, prefix `^`;
one opaque type, `using info = decltype(^::)`; splicers `[: r :]`, `typename[: r :]`, `&[: r :]`; and
metafunctions including `members_of`, `nonstatic_data_members_of`, `bases_of`, `enumerators_of`,
`offset_of`, `size_of`, `type_of`, `parent_of`, `substitute`, `reflect_invoke`, `extract<T>`,
`data_member_spec`, `define_class`, plus some sixty `is_*`/`has_*` predicates.
[P2996R7](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2996r7.html)
**Cannot:** `define_class` completes an *incomplete* class with non-static data members and nothing
else — no member functions, no bodies, no bases — and the paper concedes it is "not nearly as powerful
as generalized code injection". A complete class cannot be reopened. Nothing generates a function
body. Statement injection at an arbitrary program point does not exist; code injection is named only as
future work. Expression reflection was deferred. Everything is `consteval` introspection: **no
instrumentation, no hooks, no lifetime observation of any kind.** C++ has the reflection Fin needs and
none of the injection — a collector built on P2996 could compute a pointer map and would have nowhere
to put a write barrier.

### 1.3 D — `__traits`, CTFE, mixins, and `object.RTInfo`

D is the most important entry in this survey, for a reason that is not its macro system. **D ships a
working precise garbage collector whose entire compile-time component is one per-type template in the
standard library, and that collector injects no code at any program point.** Everything else in this
survey is a metaprogramming facility looking for a use; D is the one case where the exact use Fin
cares about was actually built and shipped. It is therefore both the strongest evidence that this is
possible and the sharpest available measurement of what it costs.

**Exposes — `__traits`.** Seventy keywords. [dlang.org/spec/traits.html](https://dlang.org/spec/traits.html)
Including `allMembers`, `derivedMembers`, `getMember`, `getOverloads`, `getAttributes`, `isSame`,
`compiles`, `identifier`, `fullyQualifiedName`, `parent`, `child`, `classInstanceSize`,
`classInstanceAlignment`, `getVisibility`, `getLocation`, `getLinkage`, `getVirtualMethods`,
`initSymbol`, `toType`, `needsDestruction`, `isPOD`, `getBitfieldOffset` — and
**`getPointerBitmap`**, the closest prior art anywhere to Fin's `pointer_offsets`.

`__traits(getPointerBitmap, T)` returns "an array of size_t describing the memory used by an instance
of the given type". Element `[0]` is the size of the type ("for classes it is the
classInstanceSize"); the remaining elements are bitmasks in which "there are `T.sizeof /
size_t.sizeof` possible pointers represented by the bits of the array values". The documented purpose
is exactly Fin's: "This array can be used by a precise GC to avoid false pointers." A class's vtable
pointer and monitor field are not marked; a plain function pointer is "not a GC managed pointer";
slices contribute `{length, ptr}` and delegates `{context, func}`.

**Exposes — `object.RTInfo`, the mechanism that matters.** DMD instantiates `object.RTInfo!T` for
every struct and class during `semantic3` (`semanticRTInfo`, `compiler/src/dmd/semantic3.d`),
CTFE-evaluates it, and burns the result into that type's `TypeInfo.m_RTInfo` (`glue/toobj.d`). The
druntime template returns the pointer bitmap. The hook by which the compiler finds it is a hardcoded
name test in `templatesem.d` — `if (sc._module.ident == Id.object) { if (tempdecl.ident == Id.RTInfo)
Type.rtinfo = tempdecl; }` — and it is DMD's **only** name-based hook into user-replaceable code
anywhere in the compiler.

Read the shape carefully, because it is not the shape anyone would guess. The compiler **pulls**,
once per type, at layout-completion time; the library **returns a value**; the compiler **stores** that
value against the type and emits it. No injection site is involved. No mutable compile-time table is
involved. The library never enumerates the program's types — the compiler enumerates, the library
answers. This is the projection principle of §2.2 arrived at independently and shipped in production,
and it is why §3.10 of this design adds a third mechanism rather than forcing the pointer map through
the event system.

**Exposes — `@__ctfe`.** D's baseline rule is that "All functions that execute in CTFE must also be
executable at run time… The semantics of a function cannot depend on compile time values of the
function", which makes `int foo(string s) { return mixin(s); }` illegal because the run-time code
cannot be generated. D later had to add an attribute to escape its own rule: `@__ctfe` "ensures a
function is only called with CTFE, disabling object code generation… cannot be called in a runtime
context, and taking its address at runtime is an error." That is `@special`, arrived at years late.
D also carries a `__ctfe` boolean pseudo-variable so an *ordinary* function can branch at compile
time, statically folded. **Fin needs no equivalent of the pseudo-variable, because `@special` is a
distinct function kind rather than a mode of an ordinary function** — that is a real simplification
over D and is named here as one rather than left as an absence.

**Cannot — the definitive list.** D cannot inject a statement into a function body it did not write:
statement mixins and `scope(exit)` are `Statement` productions and expand lexically, where written.
It cannot add members to an already-declared type from outside its body — there are no partial types,
and UFCS is call-syntax sugar only, so `__traits(hasMember, S, "write")` is **false** for a
UFCS-callable free function. It cannot define a new pragma (unrecognised pragmas are a required
diagnostic). It cannot register a compiler callback or an AST pass — no plugin switch exists, and
`dmd.frontend` hosts the compiler rather than extending it. UDAs are inert: "There is no runtime
component to them." And `getPointerBitmap` returns **bits, not types** — it says *word 3 holds a
pointer*, never *word 3 holds a pointer to `Node`* — so D's collector is precise about where pointers
are and blind about what they point to.

**Cannot — and what D did about it.** This is the load-bearing observation. Every time D's designers
wanted library-declared, type-triggered, use-site-free semantics, **they hardcoded it into the
compiler.** `@mustuse` is the clean example: it is exactly that shape, and delivering it required a
dedicated compiler module, `compiler/src/dmd/mustuse.d`. A D user cannot author an equivalent. Even
`RTInfo` — the one genuinely library-side mechanism — reaches the compiler through a hardcoded string
comparison against the name `RTInfo` in the module named `object`.

**Two holes in D's automatic mechanisms, both documented rather than closed.** Unions: "a union itself
never has a destructor. When a union goes out of scope, destructors for its fields are not called. If
those calls are desired, they must be inserted explicitly by the programmer." Interfaces:
`getTypePointerBitmap` takes the top-level-class branch for an interface but sizes it with `t.size()`,
so an interface type yields a pointer-sized size with an all-zero bitmap — even though an interface
reference *is* a GC pointer. (Source reading, not spec text; no filed issue.) Both are the same class
of defect and both are the failure mode to design against: **a reflection answer that is silently
wrong for exactly one type constructor.** Neither would be caught by a test that did not already
suspect it. §4 turns both into design questions.

**One cost lesson.** D's bitmap is one bit per pointer-sized word, so `struct S { int[200000000] x; }`
costs roughly 0.3 s of semantic analysis to produce a bitmap of zeroes. DMD PR 22289 (open) changes it
to return size alone for pointer-free types, and the PR notes this "may break existing code" that
assumed array length tracks type size. **Whatever Fin's layout answer is, its cost must be counted in
fields, not in bytes.**

**One composition lesson.** D gets a large part of its use-site-free behaviour from a plain language
rule, not from metaprogramming: "If the struct has a field of another struct type which itself has a
destructor, that destructor will be called at the end of the parent destructor. **If there is no
parent destructor, the compiler will generate one.**" So a library type used as a field gets its
cleanup run with nothing written at the use site beyond declaring the field, and
`__traits(needsDestruction, T)` lets a library interrogate that at compile time. Fin's composition
rule determines how much its event system actually has to carry, and is a design question (§4).

**One confirmation of a ruling already made.** D requires that a CTFE function "must have a
*FunctionBody*" — which is D's stated reason there is no compile-time I/O, and independent
confirmation of Fin's externs-forbidden-inside-`@special` constraint. And D's spec concedes: "If the
function goes into an infinite loop, it may cause the compiler to hang." Fin's interpretability line
(ADR 0006: five statement forms, no control flow) makes a compiler hang **impossible by
construction**. That guarantee is the currency this design spends when it widens the line, and §4.R2
spends it explicitly rather than letting it lapse.

### 1.4 Zig — `comptime`

One language for program and metaprogram, and genuine type values. `std.builtin.Type` is a 24-tag
union; `Struct` carries `layout`, `backing_integer`, `fields`, `decls`, `is_tuple`, and `StructField`
carries `name`, `type`, `default_value_ptr`, `is_comptime`, `alignment`.
[lib/std/builtin.zig](https://raw.githubusercontent.com/ziglang/zig/master/lib/std/builtin.zig)
Plus `@typeInfo`, `@TypeOf`, `@offsetOf`, `@sizeOf`, `@alignOf`, `@bitOffsetOf`, `@hasField`,
`@hasDecl`, `@compileError`, `@Type`.
**Cannot:** the `Declaration` payload is, literally and entirely, `name: [:0]const u8` — one field. So
`@typeInfo` reports declaration *names* and nothing else: no types, no values, no bodies. For
construction the consequence is documented: reifying a type with declarations was requested in 2020 and
"The compiler currently errors when the `.decls` slice is non-empty"; the issue is **closed as not
planned**. [ziglang/zig#6709](https://github.com/ziglang/zig/issues/6709) A Zig metaprogram can build a
struct's *fields* and can never build its *methods*, cannot see a function body, and cannot inject code
where it does not lexically sit. There is no hook on scope exit, allocation or function entry — `defer`
is written at the site, and allocator instrumentation is an ecosystem-wide convention of passing an
allocator parameter, not a compiler mechanism.

### 1.5 Nim — macros, term rewriting, and lifetime hooks

The closest existing thing to what Fin is trying to be, and the most instructive.
**Term-rewriting macros:** a macro may carry "not only a *name* but also a *pattern*", and the pattern
is "searched for after the semantic checking phase of the compiler", with real pattern operators (`|`
ordered choice, `~` negation, `*` flattening, `**` reverse-polish, `expr{param}` binding), applied
recursively up to a limit. [manual_experimental.html](https://nim-lang.org/docs/manual_experimental.html)
That is a whole-program syntactic rewrite triggered by an import — stronger than Rust or Zig.
**Lifetime hooks:** six per-type hooks — `=destroy(x: T)`, `=wasMoved(x: var T)`,
`=sink(dest: var T; source: T)`, `=copy(dest: var T; source: T)`, `=trace(dest: var T; env: pointer)`,
`=dup(x: T): T`. [destructors.html](https://nim-lang.org/docs/destructors.html) `=destroy` runs "when
they go out of scope or when the routine they were declared in is about to return"; `=trace` exists for
the `--mm:orc` cycle collector; the compiler "generates implicit hooks for all types in *strategic
places*"; hooks lift structurally through tuples and objects.
**Cannot:** the hooks are **type bound** — one per type per hook, designed for the type's own author, so
a library cannot install a `=destroy` that applies to types it did not write. Nim also hit exactly the
phase-ordering problem Fin must solve, with the diagnostic "destructor for 'f' called here before / it
was seen in this module", and its answer pushes the burden onto the programmer: define the hook before
it is used. Term rewriting is constrained too — matching runs *after* constant folding (so "`echo 1`
won't match a `0|1` pattern"), it is "currently greedy", a hard recursion limit silently ignores the
macro past it, and "a term rewriting macro should not change the semantics anyway". Patterns match
syntax with type constraints, never layout: no size, alignment or offset in the pattern language.

### 1.6 Julia — `@generated`

A generated function is expanded once argument *types* are known: "In the body of the generated function
you only have access to the types of the arguments – not their values", and you "return a quoted
expression". [Metaprogramming](https://docs.julialang.org/en/v1/manual/metaprogramming/)
**Cannot:** the restriction list is severe and explicit. Generated functions "must not mutate or observe
any non-constant global state" — "they must be completely pure"; may only call functions defined before
them (world age); `eval` "is disallowed"; "cannot define a closure or generator"; and because generation
may run "once, repeatedly, or seemingly never", "you should never write a generated function with side
effects". The purity rule alone forbids the central move of a Fin handler: **a Fin handler mutates
compile-time state** — it registers a type in a table, it accumulates a pointer map. Julia forbids
exactly that.

### 1.7 Terra

A two-language design: Lua is the metaprogram, Terra the compiled language, and Terra functions and
types are first-class *Lua values*, so the metaprogram has full I/O, mutable state and a real module
system while constructing the program. Quotes and escapes stitch the two together.

**The one thing Terra does better than anyone, and that this design adopts (§3.2):** it splits layout
into **two moments** and enforces which questions are legal in each. `__getentries(self)` runs while the
type is still *incomplete* and **decides** the layout — asking anything requiring completeness inside it
is a documented error. `__staticinitialize(self)` runs "after the type is complete but before the
compiler returns to user-defined code" and **observes** it, its documented purpose being to build
vtables and read field offsets via `terralib.offsetof`.

**Cannot:** the metaprogram is not the same language as the program — precisely the property Fin
rejects, since a Fin `@special` is Fin. A library author writes in two languages and the compiled
language cannot reason about itself. Terra has **no alignment query at all** — `sizeof` and `offsetof`
only, both thin LuaJIT FFI wrappers — so a Terra metaprogram that knows offsets cannot compute padding;
Fin must not reproduce that gap (§2.5 keeps `align_of`). And function mutation is
**replacement-only**: `adddefinition` requires the target be undefined, `resetdefinition` errors once
compiled, both take a whole replacement function, and there is no public way to read the body being
replaced. Terra therefore confirms D from the other direction — **construction is supported, amendment
is not, in both languages.** Fin's events are amendment, which is the thing neither has.

### 1.8 Racket — phases and the lifting API

Where the *injection* prior art lives. `local-expand(stx, context-v, stop-ids, [intdef-ctx])`, where
`stop-ids` "controls how far local-expand expands stx". And the **lifting family**:
`syntax-local-lift-expression(stx) → identifier?` coordinates with the module expander "to bind the
generated identifier to the expression stx", placing a run-time expression "at the module's top level,
just before the expression whose expansion requests the lift"; plus
`syntax-local-lift-values-expression`, `syntax-local-lift-context` (whose result is "useful for caching
lift information to avoid redundant lifts"), `syntax-local-lift-module`,
`syntax-local-lift-module-end-declaration` and `syntax-local-lift-require`.
[stxtrans.html](https://docs.racket-lang.org/reference/stxtrans.html) Separately, redefining
`#%module-begin`, `#%app` and `#%datum` lets a `#lang` intercept and rewrite an entire module.
**Cannot:** Racket is dynamically typed. There are no types at expansion time in the sense Fin needs,
and no size, no alignment, no field offset, and no notion of which words of a value hold pointers.
Racket has the injection Fin needs and none of the reflection — the exact mirror image of C++26.

### 1.9 Where the bar actually sits

| | Types at CT | Layout: size/offset | Which words are pointers | Inject at a non-lexical point | Lift a declaration elsewhere | Library instruments code it doesn't own | Metaprogram is the same language | Fires on a program event |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Rust proc macros | no | no | no | no | no | no (per-item) | yes | no |
| C++26 P2996 | **yes** | **yes** | no | no | no | no | yes | no |
| D `__traits` | **yes** | **yes** | **bits only** | no | no | no | yes | per-type only |
| Zig `comptime` | **yes** | **yes** | no | no | no | no | yes | no |
| Nim | partial | no | no | **rewrite only** | no | **yes** (rewrite) | yes | **per-type only** |
| Julia `@generated` | **yes** | partial | no | no | no | no (Cassette aside) | yes | no |
| Terra | **yes** | **yes** | no | no | n/a | no | **no** | no |
| Racket | no | no | no | **yes** | **yes** | **yes** | yes | no |
| **Fin, as designed** | **yes** | **yes** | **typed** | **yes** | **yes** (Q5) | **yes** | **yes** | **yes** |

**No cell in the "typed pointer map" column is filled by anyone.** D comes closest and gives
bitmasks. `compiler.layout.pointee_type_at` is the single most defensible "surpasses" claim in this
design, and it is not a stunt — ADR 0003 cannot be honoured without it.

**No row combines layout with non-lexical injection.** C++26 and Zig have the reflection and no
injection; Racket has the injection and no reflection. Fin's compiler API is the first design in this
list that puts both in one language, and the reason is structural rather than clever: Fin's
metaprogram runs *during semantic analysis* (ADR 0006), which is after types and layout exist and
before code is emitted. Rust's macros run too early, C++'s `consteval` runs in the wrong direction,
and Racket's expander runs in a language with no layout to know.

**Nobody lets an imported library instrument types it does not own with layout knowledge.** Nim's
hooks are the closest and they are type bound — the type's author writes them. Fin's events are
program-point bound and armed by the consumer, which is a different and stronger position, and it is
the one ADR 0003 requires.

**Three cautionary lessons, taken deliberately:**

1. *Nim's phase-ordering diagnostic* ("called here before it was seen in this module") is the failure
   Fin's arming phase exists to prevent. §3.4.
2. *Julia's purity rule* is a warning about what happens when the metaprogram may be re-run at the
   compiler's discretion. Fin must therefore commit that a handler runs **exactly once per event
   point** and that its results are not cached, or it inherits Julia's restriction.
3. *Rust's derive diagnostics* — an error reported in generated code the user never wrote — is the
   ergonomic failure most likely to be repeated. §3.8 makes attribution to the handler a stated
   requirement rather than a hope.


---

## 2. The component inventory

### 2.1 The two namespaces, and where enforcement bites

ADR 0012 settles the split and this design conforms to it. `compiler.components.<name>` is a
**component reference** — capability negotiation and nothing else. `compiler.<name>.<member>` is the
**use** of a component: its operations and its constants. The deciding test is ADR 0012's: *is it about
a component, or through one?*

Two rulings follow from ADR 0012 that constrain the inventory below, and one refinement of mine is
**withdrawn** by it.

**(a) Capability queries are postfix on a reference, never free functions.** ADR 0012: "Any third
segment under `compiler.components` is an operation on a reference, never a component name." So the
grant layer's surface is not `compiler.components.available("events")` — that spelling is exactly the
ambiguity ADR 0012 rules out, since `available` would be readable as a component name. It is:

| Form | Meaning |
| --- | --- |
| `compiler.components.<name>` | The reference itself. The only form the corpus uses, and the only form legal inside `#[use(...)]`. |
| `compiler.components.<name>.present()` `<bool>` | Does this compiler have the component at all? **Must be answerable for an absent component** — that is the whole point. |
| `compiler.components.<name>.version()` `<int>` | Contract version. ADR 0012's own example. |
| `compiler.components.<name>.granted()` `<bool>` | Did the enclosing `@special` declare it in `#[use(...)]`? |
| `compiler.components.<name>.name()` `<string>` | For diagnostics. |

`present()` carries a requirement the rest of the design must respect: `compiler.components.gc.present()`
has to *evaluate to `false`* rather than fail to resolve, or forward compatibility is unimplementable.
That makes the component registry a lookup that returns a null-ish reference, not a name-resolution
error — an implementation constraint on wave 4, recorded in §5.

**(b) Constants live with the operations that consume them.** ADR 0012 puts enforcement on the
operations layer, and treats `memory.fin:26` and `:37` — which grant only `system` and read
`compiler.enums.InBytes` — as genuine violations. **I previously proposed that reading a constant
should need no grant. ADR 0012 decides otherwise and I withdraw it.** But the withdrawal has a
consequence that changes the inventory, and it is an improvement:

> If constants need grants, then filing a constant under a component other than the one whose
> operations consume it forces the consumer to grant *two* components to make *one* call.

`compiler.enums.InBytes` is the proof: its only use is as an argument to
`compiler.system.get_available_memory`, so filing it under `enums` is what makes `memory.fin` wrong. My
earlier draft made this mistake systematically — it proposed `compiler.enums` as "the flat home for all
API constants" (`Kind*`, `Vis*`, `ExitNormal`, `MovedYes`, …), which under ADR 0012 would make
`#[use(compiler.components.enums)]` a grant that nearly every `@special` in the language needs, i.e. a
grant that has stopped carrying information. **Withdrawn.** The rule is now:

> **A constant lives under the component whose operations take it or return it.**
> `compiler.types.Kind*`, `compiler.structs.Vis*`, `compiler.events.ExitNormal`/`ExitBlamed`/`Moved*`.

The four corpus paths are unchanged, so `compiler.enums.InBytes` stays where the corpus puts it and is
**grandfathered**: the one constant filed away from its consumer, fixed by the header edit ADR 0012
predicts rather than by moving it. That is the whole cost of conforming, and it is one line in
`memory.fin`. Q2 asks the owner to confirm the grandfathering rather than relocate the constant.


### 2.2 The spine of the design: "the compiler iterates, the library decides"

This is the principle that makes the whole surface implementable in wave 4, so it comes before the
table.

The measured interpretability line has **no control flow at all** and `==` as its only operator. Any
operation that returns a *list* — a type's fields, the live variables in a scope, the offsets of the
pointers in a struct — cannot be consumed without a loop. So a naive list-returning API forces `if`,
a loop, arithmetic, comparison operators and an array value into wave 4 before the first useful
handler can be written. That is most of a language.

The way out is that **the interesting work is at run time, not compile time.** A tracing collector's
mark loop runs in the compiled program. The *handler* only has to hand the collector a table and a
call. So every list-valued operation ships in two forms:

- a **value form** — returns the list as a Fin value. Requires arrays and loops. **Deferred.**
- a **projection form** — the compiler performs the fold and returns a scalar, a `{K,V}` prototype,
  or a quote that already contains the whole list as literal syntax. **Wave 4 ships only these.**

So instead of

```fin
// needs arrays, a loop, arithmetic — none of which the line has
let offs <[uint]> = compiler.layout.pointer_offsets(t);
for (let i <int> = 0; i < offs.length; i++) { ... }
```

a handler writes

```fin
// no control flow, no arithmetic, no arrays — the compiler did the walking
return compiler.code.splice(quote {
    CortexGC::register_type($tid, $map);
}, "tid", compiler.types.typeid_quote(t),
   "map", compiler.layout.pointer_map_quote(t));
```

Naming: a **projection** is a component operation that folds a compiler-side list into a value the
current interpretability line can already hold. The word is new; it is proposed for `CONTEXT.md`.

Every row in the tables below is marked `P` where it is a projection that exists only because the
value form is deferred. If the owner later decides wave 4 should include loops and arrays, the
projections stay useful (they are how you emit a table) but stop being load-bearing.

### 2.3 Reading a `$type`: through the component, not off the value

Two possible surfaces for `MetaType`'s agreed minimum (`name`, `typeid`, `size`, `align`, `kind`,
`fields`, `methods`, `implements(iface)`, `pointer_offsets`):

- **(A) member access on the value** — `t.name`, `t.size`, `t.fields`.
- **(B) component operations** — `compiler.types.name_of(t)`, `compiler.layout.size_of(t)`.

The corpus does **(B)**: `types.fin:23` reads a field of a compiler-side object with
`compiler.structs.select_field::<int>(compiler.types.gettype::<T>(), "TypeID")` — by string, through
a component. The one counter-example is `enums.fin:21`, `enum_member._keyid`.

This design recommends **(B), and `$type` stays opaque.** The decisive argument is not taste, it is
enforceability: `t.size` is a member access, not a component call, so under (A) **reading layout
requires no grant at all** and `#[use(compiler.components.layout)]` becomes undeclarable and
unenforceable. Surface (A) would put the most safety-critical part of the API — the part a garbage
collector depends on for correctness — outside the mechanism that exists to govern it.

`enums.fin:21` becomes `compiler.enums.keyid_of(m)`, which is a one-line stdlib edit, not a language
change. See Q3.

### 2.4 Tier 1 — the four the corpus names

These must exist with these exact paths. All 13 corpus paths appear below unchanged.

**`compiler.components.types` → `compiler.types.*`** — the type table: identity, resolution,
comparison, kind.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `cmp_types` | `(a: $type, b: $type) <int>` | read-only | — | **Corpus** `error.fin:25`. Returns `-1` for unequal; `error.fin:26` compares against `-1`. |
| `ct_any` | `(v: any) <$type>` | read-only | — | **Corpus** `types.fin:89`. The compile-time type of a value. Diagnostic if `v` is a runtime-typed `Any`. |
| `gettype` | `::<T>() <$struct>` | read-only | — | **Corpus** `types.fin:23`. Turbofish, no value argument. Returns the compiler's own `TypeInfo`-shaped struct handle. |
| `typefrom_typeid` | `(tid: uint) <$type>` | read-only | — | **Corpus** `types.fin:82`. Diagnostic if `tid` names no type. |
| `typeid_of` | `(t: $type) <uint>` | read-only | — | The inverse. `types.fin:23` gets it the long way round via `select_field`. |
| `name_of` | `(t: $type) <string>` | read-only | — | `MetaType.name`. |
| `kind_of` | `(t: $type) <int>` | read-only | — | `MetaType.kind`, valued from `compiler.types.Kind*` (§2.1b). |
| `implements` | `(t: $type, i: $interface) <bool>` | read-only | — | `MetaType.implements`. Backs `@implements` (`literal_interface.fin:4`). Argument order is checked because `$struct` and `$interface` are distinct types. |
| `is_comptime_type` | `(t: $type) <bool>` | read-only | — | False for `Any`/`Any<...>`/`nullptr`, the `#[RT]` types in `types.fin:66,71,75`. This is what makes `ct_any`'s diagnostic expressible. |
| `typeid_quote` | `(t: $type) <quote>` | read-only | `P` | Projection: the typeid as literal syntax, for splicing. |

**`compiler.components.structs` → `compiler.structs.*`** — the structure of a struct, class or
interface.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `select_field` | `::<R>(s: $struct, name: string) <?R>` | read-only | — | **Corpus** `types.fin:23`. Nullable return, denullified at the call site with `?`. Note it is generic in the *result* type. |
| `has_field` | `(s: $struct, name: string) <bool>` | read-only | — | The check that makes `select_field`'s denullify safe. |
| `field_count` | `(s: $struct) <int>` | read-only | — | |
| `field_type` | `(s: $struct, name: string) <?$type>` | read-only | — | |
| `field_visibility` | `(s: $struct, name: string) <int>` | read-only | — | Valued from `compiler.structs.Vis*` (§2.1b). Must distinguish `pub`, `priv` and `readonly` — and `readonly` **does not exist in the compiler** (§0.2). |
| `fields_quote` | `(s: $struct) <quote>` | read-only | `P` | Projection: every field as an array literal of `{name, typeid, offset}`. This is what a reflection library emits. |
| `method_count` / `has_method` / `method_type` | | read-only | — | `MetaType.methods`. |
| `has_destructor` | `(s: $struct) <bool>` | read-only | — | Already on `StructType.hpp:32`. A collector must know whether scope exit already runs one. |
| `parents_quote` | `(s: $struct) <quote>` | read-only | `P` | `StructType.hpp:25` holds `parents`; classes (`#[class]`) need the chain. |

**`compiler.components.enums` → `compiler.enums.*`** — enum reflection, plus one grandfathered constant.

The corpus puts two unrelated things here: `resolve_id`, an operation on user enums, and `InBytes`, a
unit constant whose only consumer is `compiler.system.get_available_memory`. Under §2.1(b) the constant
belongs with its consumer, and `InBytes` is the one place the corpus disagrees. The 13 paths are the
specification, so it **stays and is grandfathered** — `compiler.enums` is *enum reflection*, and
`InBytes` is a documented exception rather than the seed of a general constants dumping ground.

| Member | Kind | Signature | Note |
| --- | --- | --- | --- |
| `InBytes` | **constant** | `uint` | **Corpus** `memory.fin:30,31,39`. Read as a value, never called. Grandfathered exception to §2.1(b): a `system` constant filed under `enums`. The reason `memory.fin:26,37` must gain an `enums` grant. |
| `InKilobytes`, `InMegabytes`, `InGigabytes` | constant | `uint` | The other units the same argument admits. Filed here for consistency with `InBytes`, not because it is right. |
| `resolve_id` | operation, read-only | `(value: EnumType) <int>` | **Corpus** `enums.fin:11`. |
| `keyid_of` | operation, read-only | `(m: $enum_member) <int>` | Replaces `enum_member._keyid` at `enums.fin:21` and keeps `$enum_member` opaque (§2.3). |
| `member_count` / `member_name` / `member_by_name` | operation, read-only | | |
| `payload_type` | operation, read-only | `(m: $enum_member) <?$type>` | `Result<T,U>`'s `Ok(T)`. `EnumDeclaration` has no field that could hold this today (§0.2). |

Every other enumerated constant moves to the component that consumes it, per §2.1(b):

| Constants | Home | Consumed by |
| --- | --- | --- |
| `KindType`, `KindStruct`, `KindInterface`, `KindEnum`, `KindPrimitive`, `KindPointer`, `KindArray`, `KindFunction`, `KindPrototype` | `compiler.types.*` | `compiler.types.kind_of` |
| `VisPublic`, `VisPrivate`, `VisReadonly` | `compiler.structs.*` | `compiler.structs.field_visibility` |
| `ExitNormal`, `ExitBlamed` | `compiler.events.*` | the `variable_scope_exit` payload |
| `MovedYes`, `MovedNo`, `MovedMaybe` | `compiler.events.*` | the same payload. `Maybe` is the honest third case — moved on one branch only. Without it a collector must either double-free or leak. |

The payoff is concrete: a handler that reads its own event payload grants `compiler.components.events`,
which it needs anyway to have been armed. Under the withdrawn design it would have had to grant
`enums` as well, for a constant that has nothing to do with enums.


**`compiler.components.system` → `compiler.system.*`** — host and target facts.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `get_total_memory` | `(unit: uint) <uint>` | read-only | — | **Corpus** `memory.fin:30,39`. |
| `get_available_memory` | `(unit: uint) <uint>` | read-only | — | **Corpus** `memory.fin:31,39`. |
| `get_memorycard_model` | `() <string>` | read-only | — | **Corpus** `memory.fin:29`. |
| `pointer_size` | `() <uint>` | read-only | — | The single most-needed target fact; a pointer map is meaningless without it. |
| `target_triple` | `() <string>` | read-only | — | ADR 0010 pins six triples. |
| `endianness`, `max_align` | | read-only | — | |

Note what `get_*_memory` are: they read the **host that is compiling**, not the target that will
run. A library that branches on them produces a different program on a different build machine, which
breaks the reproducibility ADR 0010 exists to guarantee. See Q11.

### 2.5 Tier 2 — required by a ratified commitment

**`compiler.components.layout` → `compiler.layout.*`** — sizes, alignments, offsets, pointer maps.
This component exists because ADR 0003 commits to a tracing collector written in Fin and
`pointer_offsets` is non-negotiable. It has **no substrate today** (§0.2).

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `size_of` | `(t: $type) <uint>` | read-only | — | `MetaType.size`. |
| `align_of` | `(t: $type) <uint>` | read-only | — | `MetaType.align`. |
| `offset_of` | `(s: $struct, field: string) <?uint>` | read-only | — | `MetaType.fields[].offset`. |
| `pointer_count` | `(t: $type) <int>` | read-only | — | Zero answers "does this type need tracing at all" in one call. |
| `pointer_offset_at` | `(t: $type, i: int) <uint>` | read-only | — | Value form of `pointer_offsets`, indexed so it needs no array — but still needs a loop to walk, so it is **not** what wave 4's handlers use. |
| `pointee_type_at` | `(t: $type, i: int) <$type>` | read-only | — | **This is the surpassing bit.** D's `getPointerBitmap` gives a bitmask and nothing else; knowing a word holds *a pointer to `Node`* rather than *a pointer* is the difference between a precise typed collector and a conservative one. |
| `pointer_map_quote` | `(t: $type) <quote>` | read-only | `P` | Projection: the whole map — offsets, pointee typeids, and the kind of each (owned, borrowed, weak, interior) — as one array literal ready to splice. **The operation the collector actually calls.** |
| `is_layout_final` | `(t: $type) <bool>` | read-only | — | False before `struct_layout_finalised` has fired for `t`. |
| `request_header_words` | `(s: $struct, n: uint) <noret>` | **effect** | — | Legal **only** inside `struct_layout_deciding`. Reserves `n` pointer-sized words ahead of the object for a collector's mark bits or forwarding pointer. Additive across handlers, so two collectors get two headers rather than a conflict. Speculative — no stdlib consumer, but ADR 0003's collector has no other way to obtain per-object state. |
| `request_min_align` | `(s: $struct, a: uint) <noret>` | **effect** | — | Same phase restriction. Additive by maximum. A collector that tags low pointer bits needs this. |

**Phase legality is part of this component's contract, not an afterthought.** Layout is two moments
(§1.7, §3.2). Every operation above is legal in exactly one of them:

| Phase | Legal | Illegal |
| --- | --- | --- |
| `struct_layout_deciding` | `request_header_words`, `request_min_align`, `field_count`, `field_type`, `has_field`, `kind_of`, `name_of` | `size_of`, `align_of`, `offset_of`, `pointer_*`, `pointer_map_quote` |
| `struct_layout_finalised` and everywhere after | everything read-only | `request_header_words`, `request_min_align` |

Calling a layout query in the decide phase must be a **diagnostic naming the phase**, never a zero or a
placeholder. That is the whole lesson of D's interface bitmap (§1.3): an answer that is confidently
wrong is worse than an answer that is refused. `is_layout_final` exists so a handler shared between the
two phases can ask rather than guess.

Note also, against Terra: `align_of` is present. Terra has `sizeof` and `offsetof` and **no alignment
query at all** (§1.7), which makes padding uncomputable — a collector that cannot see padding cannot
tell a pointer-free gap from an untraced word.

**`compiler.components.events` → `compiler.events.*`** — arming, and reading the current event.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `enable` | `(h: handler) <noret>` | **effect** | — | ADR 0007's arming call. The one operation in the API whose whole purpose is a side effect. |
| `disable` | `(h: handler) <noret>` | **effect** | — | Needed for a test to turn a collector off, and for `finn` to build a program twice. |
| `is_enabled` | `(h: handler) <bool>` | read-only | — | |
| `armed_count` | `(event: int) <int>` | read-only | — | How a library detects that a second collector is armed and reports it itself — the only defence ADR 0007 leaves available against "two garbage collectors armed at once will both free the same variable". |
| `handler_name_at` | `(event: int, i: int) <string>` | read-only | — | So that report can name the other one. |

**`compiler.components.code` → `compiler.code.*`** — the write side. Everything a handler is allowed
to produce goes through here.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `empty` | `() <quote>` | read-only | — | The "inject nothing" answer. Needed by `struct_layout_finalised`, whose whole contract is to return one. |
| `splice` | `(q: quote, name: string, v: quote) <quote>` | read-only | — | Substitutes one `$name` unquote in `q`. Variadic in the corpus's style (`(q, n1, v1, n2, v2, ...)`). This is the composition primitive. |
| `concat` | `(a: quote, b: quote) <quote>` | read-only | — | Sequence two injections. |
| `lit_int`, `lit_uint`, `lit_string`, `lit_bool` | `(v) <quote>` | read-only | — | Lift a Fin value to syntax. Without these `splice` has nothing to splice. |
| `ident` | `(name: string) <quote>` | read-only | — | An identifier by name. The unhygienic escape hatch; see Q6. |
| `fresh` | `(hint: string) <quote>` | read-only | — | A hygienic name no user code can capture. `parser.y:1409` represents unquotes as `Identifier("$"+name)`, a string hack with no hygiene at all. |
| `lift_to_module_end` | `(q: quote) <noret>` | **effect** | — | **Proposed extension to ADR 0007.** Declarations only, landing at the end of the declaring module. A tracing collector needs a static per-type metadata table in the program, and the event point is the wrong place for it. Racket's `syntax-local-lift-module-end-declaration` is the prior art. See Q5. |

**`compiler.components.scopes` → `compiler.scopes.*`** — what is live where. ADR 0007 grants a handler
the right to read "the other variables in scope"; this is that right made callable.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `live_count` | `() <int>` | read-only | — | At the current event point. |
| `live_name_at` / `live_type_at` | `(i: int)` | read-only | — | Value form; needs a loop. |
| `live_pointers_quote` | `() <quote>` | read-only | `P` | Projection: every live pointer-typed binding at this point, as an array literal of addresses. **This is the shadow stack a precise collector needs**, and no other language will build it for you. |
| `enclosing_function` | `() <function>` | read-only | — | |
| `depth` | `() <int>` | read-only | — | Block nesting. |
| `is_function_scope` | `() <bool>` | read-only | — | Distinguishes `const.fin:42`'s "goes out of scope of the function" from a bare block (ADR 0011). |

**`compiler.components.diag` → `compiler.diag.*`** — a library's own diagnostics. Without this a
handler's only way to refuse is `blame`, which aborts everything; a library that wants to report
three bad types must be able to.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `error` | `(msg: string) <noret>` | **effect** | — | Sets `hasErrors()`, compilation continues. Must route through `DiagnosticEngine` — ADR 0009 forbids a stray byte on the JSON path. |
| `warning` / `note` | `(msg: string) <noret>` | **effect** | — | |
| `error_at` | `(loc: source_loc, msg: string) <noret>` | **effect** | — | Points at user code rather than at the library. |

### 2.6 Tier 3 — required by a corpus construct with no other home

**`compiler.components.symbols` → `compiler.symbols.*`** — the program's declarations.
`literal_struct.fin:27` writes `@defined("printf")`, and there is nowhere else for it to live.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `defined` | `(name: string) <bool>` | read-only | — | Backs `@defined`. |
| `kind_of` | `(name: string) <int>` | read-only | — | Function, type, variable, macro, namespace. |
| `type_of` | `(name: string) <?$type>` | read-only | — | |
| `resolve` | `(name: string) <?symbol>` | read-only | — | |
| `count_with_attribute` / `name_with_attribute_at` | `(attr: string, ...)` | read-only | — | Every declaration carrying `#[gc]`. This is how a library finds its own opt-ins without the user listing them. |

**`compiler.components.attrs` → `compiler.attrs.*`** — attribute reflection. Two corpus constructs
need it: `#[slaveof(z)]` at `variables.fin:25` ("makes the lifetime of `m` be the exact lifetime of
`z`") and `#[debug]` at `readonly.fin:17` ("allows tracking"). Both are library behaviour attached
to an attribute, and neither is expressible if a handler cannot read attributes.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `has` | `(target: symbol, name: string) <bool>` | read-only | — | |
| `value` | `(target: symbol, name: string) <?string>` | read-only | — | `#[llvm_name="x"]`. `Attribute.hpp` stores `value_str` and `is_flag`. |
| `arg` | `(target: symbol, name: string) <?string>` | read-only | — | `#[slaveof(z)]` → `"z"`. `parser.y:385` parses this as a `dotted_path`. |
| `count` / `name_at` | | read-only | — | Enumerate. |
| `declare` | `(name: string, on: int) <noret>` | **effect** | — | A library declaring an attribute it owns, so an unknown attribute can be a diagnostic instead of silence. `#[attribute]` appears once in the corpus. |

**`compiler.components.memory` → `compiler.memory.*`** — the compiler's own allocation primitives.
`memory.fin:15,20` calls `@Alloc(size)` and `@Free(memory)`, commented "BUILTIN COMPILER SPECIAL
MEMORY METHOD".

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `alloc_quote` | `(size: quote) <quote>` | read-only | `P` | Emits the raw allocation call. Backs `@Alloc`. |
| `free_quote` | `(ptr: quote) <quote>` | read-only | `P` | Backs `@Free`. |
| `set_allocator` | `(fn_name: string) <noret>` | **effect** | — | **Superseded by `#[provides(allocator)]` (§3.9).** Kept in the inventory only as the fallback if Q1 rejects providers; as an imperative effect on the compiler it is a write outside the event point and so needs an ADR 0007 amendment, which the provider spelling does not. |

**`compiler.components.functions` → `compiler.functions.*`** — function reflection.

| Operation | Signature | Effect | Line | Note |
| --- | --- | --- | --- | --- |
| `name_of` / `return_type` / `param_count` / `param_type_at` / `param_name_at` | | read-only | — | |
| `is_special` | `(f: function) <bool>` | read-only | — | |
| `is_comptime_reachable` | `(f: function) <bool>` | read-only | — | **This is `#[comptime]` made queryable.** The interpretability line becomes a thing a library can check rather than a thing it discovers by failing. |
| `attribute` | `(f: function, name: string) <?string>` | read-only | — | `#[overwrite(printf)]` at `stdio.fin:31`. |
| `body_quote` | `(f: function) <quote>` | read-only | — | The function's own body as syntax. **No language in the survey gives you this for an arbitrary already-declared function** — Zig cannot see a body, Rust sees only its own token stream, Nim sees `getImpl` for a symbol it was handed. Backs `implements cast<auto>(__get)` at `collection.fin:77`, which the corpus describes as "copying the function instead of just pointing to it". |

### 2.7 Tier 4 — speculative, argued, and cuttable

Each of these has a reason but no corpus consumer. They are separated so the owner can cut the tier
without unpicking anything above it.

| Component | Operations | Why it might be needed |
| --- | --- | --- |
| `compiler.source.*` | `file`, `line`, `column`, `span_of`, `text_of` | So a library's diagnostics point at user code. Zig has `@src`, C++ has `std::source_location`, D has `__traits(getLocation)`. Cheap, and `ASTNode` already carries a location. |
| `compiler.modules.*` | `current`, `count`, `name_at`, `imports_of`, `order_index_of` | Handler order across modules must be a **total** order, and ADR 0007 says "import order", which is not total in a DAG. See Q10. |
| `compiler.generics.*` | `instance_count`, `instance_arg_at`, `is_erased`, `erasure_marker_of` | A handler that must act once per monomorphisation. ADR 0002 carries erasure forward as a lowering decision; nothing exposes it. |
| `compiler.interp.*` | `is_comptime_known(v)`, `eval_budget`, `set_eval_budget` | Wave 4's exit criterion says "a `@special` given a runtime argument is rejected with that argument named". If `is_comptime_known` is callable, that rule is a library's, not a hard-coded analyzer check. Zig's `@setEvalBranchQuota` is the budget prior art. |

### 2.8 Count and shape

Four components named by the corpus, five required by a ratified commitment, four required by a
corpus construct with nowhere else to live, four speculative. Seventeen total, of which **thirteen
have an identifiable consumer today**.

Every one of the corpus's 13 paths appears above unchanged. No path in the corpus was moved, renamed,
or given a different arity.

---

## 3. The event system

Nothing in this section exists in the corpus. `#[on(...)]`, `compiler.events.enable`, and every event
name are new. `#[on(variable_scope_exit)]` does at least parse today (§0.1), so the syntax ADR 0007
chose is grammatically admissible without a parser change.

### 3.1 The handler's signature *is* the event's payload

An event declares a parameter list. A handler for it must have exactly that parameter list and return
`quote`. Nothing is passed ambiently, and there is no payload struct.

```fin
#[on(variable_scope_exit)]
@special gc_on_exit(name: string, t: $type, exit_kind: int, moved: int) <quote> { ... }
```

Why this shape rather than a payload object: a payload struct means struct instances in the
interpreter value model, and struct instances are explicitly deferred by the measured
interpretability line. Parameters need nothing the line does not already have. It also buys the best
diagnostic in the system — a handler whose signature does not match its event is caught at the
declaration, naming both the expected and the written shape, before anything runs.

Anything not in the parameter list is read through a component. A handler wanting the other live
variables calls `compiler.scopes.live_pointers_quote()`; a handler wanting the enclosing function
calls `compiler.scopes.enclosing_function()`. That is ADR 0007's "read broadly" made concrete: the
payload is small and the reach is total.

### 3.2 The event set

Ten events. The right-hand column is the honest provenance, which matters because the owner should be
able to tell a commitment from a guess.

| Event | Parameters | Fires | Consumer |
| --- | --- | --- | --- |
| `variable_declared` | `(name: string, t: $type, is_mutable: bool)` | After a `let`/`const`'s initialiser is analysed and the binding enters scope. | **Corpus.** `variables.fin:25` `#[slaveof(z)]` — a lifetime library must see the declaration to read the attribute. Also where a shadow-stack push must go: at declaration, not at scope entry, because the slot holds garbage until then. |
| `variable_scope_exit` | `(name: string, t: $type, exit_kind: int, moved: int)` | At each point control leaves the binding's scope — every `return`, `break`, `continue`, fallthrough, and `blame` unwind. Once per exit path, not once per binding. | **Corpus.** `const.fin:42` "by default this will be called when compiler goes out of scope of the function"; `const.fin:52`; `deeptest3.fin:42` "if exits out of scope with no deletion it automatically calls destructor". |
| `function_entry` | `(f: function)` | Before the first statement of a body, after parameters are bound. | ADR 0003's collector: the shadow-stack frame push. Pairs with `function_exit`. |
| `function_exit` | `(f: function, exit_kind: int)` | At each `return` and at fallthrough off the end. | **Corpus.** `const.fin:42` distinguishes function scope from block scope explicitly, and ADR 0011 makes a bare block a scope, so the two are genuinely different events. |
| `assignment` | `(target: quote, value: quote, t: $type)` | On assignment where the target's type is pointer-like. Includes `p.f = q` and `p[k] = e`, so a field write is covered here and needs no separate event. | **Corpus.** `stdptr.fin`'s own/borrow/release protocol is about *rebinding*, not scope. Also the write barrier a generational collector needs. |
| `allocation_site` | `(t: $type, count: quote, dest: quote)` | At each `new`, after the allocated type is resolved. | **Corpus.** `collection.fin:52,82`, `hashmap.fin:52`, `simple_pointers.fin:21-27`, and `memory.fin:15`'s `@Alloc`. |
| `delete_site` | `(t: $type, ptr: quote)` | At each `delete`. | **Corpus.** `collection.fin:44,56`, `prototype_test.fin:21`, `simple_pointers.fin:8`. **This is an addition to the provisional five and it closes a soundness hole**: with `allocation_site` but no deallocation event, a collector armed in a program that also uses `delete` cannot know an object was freed manually, and will free it again. |
| `struct_layout_deciding` | `(s: $struct)` | Once per type, **while the type is still incomplete** — after its fields are known by name and type, before size, alignment and offsets are fixed. | **Layout is two moments, not one** (§1.7, Terra's `__getentries`). This is the *decide* moment. A handler here may contribute to the layout — request an extra header word for a mark bit, force an alignment — and **may not ask for any offset, size or alignment**, because they do not exist yet. `compiler.layout.is_layout_final(s)` is `false` throughout, and every layout query is a **diagnostic, not a wrong number**. Speculative: no stdlib consumer today, but a GC that wants a per-object header has no other way to get one. |
| `struct_layout_finalised` | `(s: $struct)` | Once per type, after its size, alignment and field offsets are fixed and before any body that mentions it is analysed. | ADR 0003. The *observe* moment (Terra's `__staticinitialize`). **Superseded as the pointer-map mechanism by `#[provides(type_metadata)]` (§3.9)**, which fires at this same moment but lets the compiler keep the answer. Retained as a broadcast event for handlers that want to observe layout without supplying the metadata slot — e.g. a diagnostic that rejects a type too large to trace. If Q1 rejects providers, this event plus `lift_to_module_end` is the fallback. |
| `loop_back_edge` | `(depth: int)` | At the jump back to a loop header. | **Argued, not deferred.** The provisional set deferred this "for lack of any consumer". It has one: a collector needs safepoints. Without a back-edge hook, a loop that allocates nothing can never be interrupted, so a concurrent or incremental collector is unbuildable and only a fully stop-the-world-at-allocation design is possible. That is a real capability being given up, and it should be given up on purpose. |
| `generic_instantiated` | `(g: $type, instance: $type)` | Once per monomorphisation. | Speculative. A collector that must register each `Collection<T>` separately needs it; one that keys on typeid alone does not. Cuttable with tier 4. |

**Deferred, and I agree they should be:** `field_access` (folded into `assignment` for writes; reads
have no consumer), `cast` (`types.fin:31` makes `cast` a library function, so a library can already
see casts it is asked to perform), `import_resolved` (no consumer).

**Not an event, deliberately:** the move-to-copy rewrite. See §3.7.

### 3.3 What a handler may do

Exactly ADR 0007, made precise:

1. **Read anything.** Every read-only operation in §2 is available, subject to `#[use(...)]`.
2. **Return one quote.** It is inserted at the event point. `compiler.code.empty()` means "inject
   nothing" and is the only legal answer for `struct_layout_finalised`.
3. **Lift declarations to module end** via `compiler.code.lift_to_module_end` — *if* Q5 is answered
   yes. This is the one proposed extension to ADR 0007.
4. **Report** via `compiler.diag.*`.

And three prohibitions that ADR 0007 implies but does not state:

- **A handler may not read injected code.** Not its own, not another handler's. Otherwise handler
  order stops merely being observable and starts being semantic, and ADR 0007's
  composition-by-construction argument fails.
- **Injected code does not fire events.** Without this rule a collector's injected
  `CortexGC::free(x)` contains a `delete`, which fires `delete_site`, which injects again, forever.
  This is the same problem Racket solves with `stop-ids` in `local-expand`, and it needs the same kind
  of answer: a fixed boundary rather than a recursion limit.
- **A handler may not arm or disarm a handler.** Arming happens in one phase and only there (§3.4).

### 3.4 Arming, and the phase problem

The owner's motivating example is two lines:

```fin
import "CortexGC.fin";
@CortexCollectorInit();
```

`@CortexCollectorInit` is a `@special` whose body calls `compiler.events.enable(...)`. The problem is
ordering: events fire *during* semantic analysis (ADR 0006), so if arming happens when the analyzer
reaches line 2, everything analysed before line 2 was analysed with no collector armed, and whether
a variable gets freed depends on where in the file it appears.

**Recommendation: a distinct arming phase.** Three phases replace one:

| Phase | What runs | What is possible |
| --- | --- | --- |
| **Collect** | Declarations are registered; `#[on(...)]` builds the subscription table. Nothing executes. | ADR 0007's reason for declaring by attribute: the whole table exists before anything runs. |
| **Arm** | Every `@special` invoked as a **top-level statement** is executed, in module order. `compiler.events.enable` is legal only here. | Arming is uniform: no body has been analysed yet, so no part of the program can be instrumented differently from any other. |
| **Analyse** | Bodies are analysed; events fire; handlers run and inject. `compiler.events.enable` here is a diagnostic. | |

The cost is a real restriction: `compiler.events.enable` cannot be called from inside `main`. The
benefit is that "which handlers are armed" is not a function of file order, and the owner's two-line
example works exactly as written — `@CortexCollectorInit();` is a top-level statement.

The alternative, arm-as-you-go, makes a collector free some variables and not others depending on
whether their function was declared above or below the init call, with no diagnostic. That is the
worst class of bug this design can produce, so it is worth a restriction to remove it.

### 3.5 Ordering when several handlers observe one event

ADR 0007 says "declaration order within a module and import order across modules". **Import order is
not a total order** — the module graph is a DAG, so two modules can be unordered with respect to each
other, and "import order" then means "whichever file the loader happened to reach first", which is a
property of the filesystem.

**Recommendation:** the total order is

1. modules in **reverse post-order of the import DAG** — every module's dependencies before it —
2. ties broken by **module path string**, ascending,
3. then **declaration order** within a module.

This is deterministic, independent of filesystem traversal, and stable under adding an unrelated
import. It preserves ADR 0007's intent ("imports first") while making it well-defined, and it is a
correction to ADR 0007's wording rather than a change to its decision. See Q10.

Handlers run in that order and their quotes are concatenated in that order at the event point.

### 3.6 Exclusivity

ADR 0007 records the gap: "There is no exclusivity mechanism. An event that genuinely admits only one
handler cannot be expressed."

**Recommendation: exclusivity is a property of the event, not of the handler**, declared by the
compiler in the event table, with three arities:

| Arity | Meaning | Diagnostic on conflict |
| --- | --- | --- |
| `broadcast` | Many handlers. All run, ordered by §3.5. Injection-only. | none |
| `exclusive` | At most one handler in the whole program. | Arming the second names **both** handlers, both `#[on(...)]` sites and both arming sites. |
| `chained` | Many handlers, each receives the previous one's quote and returns a replacement. | none, but order is semantic |

All ten events in §3.2 are `broadcast`, which keeps ADR 0007 intact for events. `exclusive` exists so
that `compiler.memory.set_allocator` — where two claimants is not a composable state — has somewhere
to live. `chained` is defined but unused; it exists so the vocabulary is there when a transforming
handler is proposed, rather than being invented under pressure.

Putting the arity on the event is what makes the diagnostic good. If exclusivity were a handler's
claim, the compiler could only say "someone else got here first"; with it on the event, the compiler
knows in the collect phase that two handlers want an exclusive slot and can name them both before
running anything.

### 3.7 What events deliberately cannot do

ADR 0007: "An ownership model that wants to rewrite a move into a copy cannot be written as an event
handler."

That is correct and should stay correct. Injection-only is the property that makes handlers compose,
and a mechanism that may *replace* a node destroys it. So the answer is not to weaken events; it is a
second mechanism.

**Recommendation: a `protocol`.** A `@special` marked `#[protocol(<operation>)]` replaces the
compiler's default lowering of one operation. Protocols differ from events on every axis that
matters:

| | Event | Protocol |
| --- | --- | --- |
| Declared with | `#[on(name)]` | `#[protocol(name)]` |
| May | inject at the event point | **replace** the node |
| How many | many, ordered | exactly one, program-wide |
| Composition | by construction | by exclusion |
| Returns | a quote to add | a quote to substitute |

Candidate protocols, all of which the corpus asks for and none of which events can serve:
`assign` (copy-versus-move — `const.fin:14` "this is allowed since the value is copied"), `allocate`
(what `new` lowers to), `deallocate`, `lifetime` (`#[slaveof(z)]` at `variables.fin:25` changes *when*
a scope exit happens, which no injection can do), `destructor` (`deeptest2.fin:46` "basic destructors
are generated by compiler by default but you can overwrite it").

`protocol` is a new word and needs a `CONTEXT.md` entry if adopted. It is deliberately not called a
handler, a hook, or an event. See Q8.

### 3.8 When a handler goes wrong

Four distinct failures, four distinct behaviours. This matters for ADR 0009: none of these may be
exit `3`, because none of them is an internal error — they are all diagnosable user or library faults
and must be exit `1`.

| Failure | Behaviour |
| --- | --- |
| Handler calls `compiler.diag.error(...)` | Recorded; that handler injects nothing at that point; compilation continues so a second bad site is also reported. Exit `1`. |
| Handler executes `blame` | The handler is aborted. One diagnostic naming the handler, the event, and the event point. Remaining handlers for that event **still run** — otherwise one library's bug silently disables another's. Exit `1`. |
| The returned quote does not analyse | Diagnostic attributed to the **handler**, with a note at the event point and the injected syntax rendered in the message. This is the failure mode that makes Rust's derive macros painful — an error in code the user never wrote, pointing at a line they did — and it is worth designing against explicitly rather than inheriting. Exit `1`. |
| Eval budget exhausted | Diagnostic naming the handler and the budget. Never a hang. Zig's `@setEvalBranchQuota` is the prior art for making this the programmer's number rather than the compiler's. Exit `1`. |
| Handler signature does not match its event | Caught in the **collect** phase, before anything runs, naming expected and actual. Exit `1`. |

### 3.9 The per-type layout question is a **provider**, not an event and not a query

ADR 0007 leaves this open in as many words: "Whether that per-type question is an event at all — as
against a plain query under `compiler.types` that no `#[on(...)]` declares — is not settled here… The
compiler API design owns the choice." This is the answer.

**Neither of the two offered spellings works, and D shows why.**

*Not a plain query.* A query is pulled by the library, so the library must decide *when* to ask. The
collector needs the pointer map of every type that reaches the heap, materialised in the compiled
program, because the mark loop reads it at run time. A query gives the library no way to enumerate the
program's types and no moment at which to do it. To make a query sufficient you would have to add a
"list all types" operation and a loop — the two things §2.2 exists to avoid.

*Not an event either.* An event can be made to work, and my earlier draft did exactly that:
`#[on(struct_layout_finalised)]` fires per type, the handler stashes the map in a compile-time table,
and `compiler.code.lift_to_module_end` emits the table at the end. It works, and it costs two things.
It needs **mutable compile-time state accumulated across handler invocations**, which is precisely
what Julia's purity rule forbids and what makes a metaprogram order-sensitive (§1.6). And it needs
`lift_to_module_end`, the one mechanism in this design that **ADR 0007 forbids** — a handler writing
somewhere other than the event point. Spending that amendment on the single most important customer is
the wrong place to spend it.

**The third shape, which is what D actually ships.** DMD does not fire an event and does not answer a
query. It *pulls* `object.RTInfo!T` once per type at layout-completion time, CTFE-evaluates it, and
**stores the returned value** against the type, emitting it into the binary itself (§1.3). The compiler
enumerates; the library answers; the compiler keeps the answer. Name it:

> A **provider** is a `@special` function the compiler calls once per subject, whose **returned value
> the compiler records and emits**. It is declared `#[provides(<slot>)]`, it is exclusive — at most one
> per slot per program — and it is memoised per subject.

| | Event | Provider | Protocol (§3.7) |
| --- | --- | --- | --- |
| Who initiates | compiler, at a program point | compiler, once per subject | compiler, at an operation |
| Returns | a quote, injected at that point | **a value the compiler stores** | a replacement node |
| Arity | broadcast, many handlers | **exclusive, one per slot** | exclusive |
| Needs compile-time mutable state | yes, to accumulate | **no** | no |
| Fires per | occurrence in the program | **type** | operation |

Three properties fall out, and each of them fixes a defect in the event spelling:

1. **No mutable compile-time state.** The provider is a pure function from a `$struct` to a value. It
   does not accumulate, so it cannot be order-sensitive, so §1.6's warning does not apply to it.
2. **No `lift_to_module_end`.** The compiler owns emission, so the library never writes outside itself.
   The provider is *within* ADR 0007's read-broadly/write-narrowly rule rather than an exception to it
   — it writes nothing at all. **This removes the pointer map from the list of things needing an ADR
   0007 amendment**, which is the single most valuable consequence of this section.
3. **Memoisation is the compiler's, and it is per type, not per use.** One evaluation per type in the
   program, which is the cost D pays and the reason D's precise GC is viable.

**Slots proposed.** Only the first has a consumer today.

| Slot | Subject | Returns | Consumer |
| --- | --- | --- | --- |
| `#[provides(type_metadata)]` | `$struct` | a quote: literal syntax for the per-type record the runtime reads | ADR 0003's collector. The pointer map. |
| `#[provides(type_name)]` | `$type` | `string` | Reflection/debug formatting without a second mechanism. |
| `#[provides(allocator)]` | — (once per program) | a quote naming the allocation function | Replaces the exclusive `compiler.memory.set_allocator` from my earlier draft, folding a second ADR-0007-forbidden mechanism into this one. |

**What the provider does *not* replace.** It is per type and returns data. It cannot instrument a
program point, so it does nothing for write barriers, scope exits, roots or safepoints. Those remain
events. The division is now clean, and it is the design's answer to "what are the mechanisms":

> **Providers** answer per-type questions and the compiler keeps the answer.
> **Events** inject code at program points and may not write anywhere else.
> **Protocols** replace one operation and admit exactly one claimant.

Three mechanisms, each doing one thing, none of them a special case of another. Q1 puts the
introduction of `provider` as a third mechanism to the owner, because it is the largest single
addition in this document and it needs an ADR of its own.

**Cost constraint, inherited from ADR 0007's last paragraph and §1.3.** A provider's returned value
must be counted in **fields, not bytes**. D's bitmap is one bit per pointer-sized word, so
`struct S { int[200000000] x; }` burns ~0.3 s of semantic analysis to produce a bitmap of zeroes, and
DMD is being changed to return size alone for pointer-free types (PR 22289, open, and it "may break
existing code"). Fin's `type_metadata` must therefore be defined so that a pointer-free type returns a
constant-size answer, and an array of `N` elements contributes the element's map once plus a stride —
not `N` entries. This is a normative constraint on the slot's contract, not an optimisation.

### 3.10 The motivating example, end to end

What ADR 0003's collector needs, mapped onto the above. This is the test of whether the design is
sufficient rather than merely large.

| Collector job | Mechanism |
| --- | --- |
| Learn each type's pointer map | `#[provides(type_metadata)]` + `compiler.layout.pointer_map_quote(s)` (§3.9). The compiler enumerates types, stores and emits. |
| Own the allocator | `#[provides(allocator)]` |
| Record every heap object | `#[on(allocation_site)]` injecting `CortexGC::record($dest, $tid)` |
| Not double-free a manual `delete` | `#[on(delete_site)]` injecting `CortexGC::forget($ptr)` |
| Find roots | `#[on(function_entry)]`/`#[on(function_exit)]` pushing and popping a frame; `#[on(variable_declared)]` registering pointer slots via `compiler.scopes.live_pointers_quote()` |
| Not free a moved-from variable | `#[on(variable_scope_exit)]`, reading the `moved` parameter — and needing `MovedMaybe`, because a variable moved on one branch only cannot be answered yes or no |
| Write barrier | `#[on(assignment)]` |
| Safepoints | `#[on(loop_back_edge)]` |
| Be armed by one line | `@CortexCollectorInit();` at top level, executed in the **arm** phase |

Nine jobs. **Eight are satisfied by mechanisms that ADR 0007 as written already permits** — the two
that previously required forbidden mechanisms (emitting the type table, owning the allocator) are now
providers, which write nothing and so need no amendment. The one genuine gap left is
`loop_back_edge`, an event the provisional set defers for lack of a consumer; §3.2 argues it has one,
namely safepoints, and Q9 puts that to the owner.

`compiler.code.lift_to_module_end` survives in the inventory (§2.5) but **no longer has a
first-party consumer**. Q5 therefore asks whether to keep it at all, and the honest recommendation is
now to **defer it** rather than amend ADR 0007 for a facility nothing in the motivating example needs.

---

## 4. Design questions for the project owner

Decisions belong to the owner. Each question below states my recommended answer and the consequence of
choosing otherwise. They are grouped so the first round unblocks wave 4 and the later rounds can be
answered while wave 4 is being built.

*Note on process: the `/grill-me` skill is reserved for explicit user invocation and refused the Skill
tool, so these are written as plain numbered questions rather than in that skill's rounds. If the owner
runs `/grill-me` against this section it should slot straight in.*

### Round 1 — blocks wave 4; nothing can be built until these are answered

**Q1. Is `provider` accepted as a third mechanism, alongside events and protocols?** (§3.9)

A provider is a `@special` the compiler calls once per subject and whose **returned value the compiler
stores and emits** — `#[provides(type_metadata)]` returning a type's pointer map. It is D's
`object.RTInfo` generalised, and D ships a working precise GC on exactly that shape.

*Recommend: yes.* It is the largest single addition in this document and needs its own ADR.
*If no:* the pointer map goes back to `#[on(struct_layout_finalised)]` accumulating into compile-time
mutable state plus `compiler.code.lift_to_module_end` to emit it. That works, but it costs an ADR 0007
amendment (a handler writing outside the event point), it makes the metaprogram order-sensitive in the
way Julia's purity rule warns about (§1.6), and it puts the amendment's whole weight on the most
safety-critical customer in the language. Answering yes means eight of the collector's nine jobs need no
amendment at all; answering no means two do.

**Q2. `compiler.enums.InBytes` — grandfather it, or relocate it to `compiler.system`?** (§2.1b)

Under ADR 0012 constants need grants, so a constant filed away from its consumer forces two grants for
one call. `InBytes`'s only consumer is `compiler.system.get_available_memory`.

*Recommend: grandfather it.* The corpus is the specification, `memory.fin:26,37` gain an `enums` grant,
and the general rule for every *new* constant is "lives with the component that consumes it".
*If relocate:* cleaner rule with no exception, but it edits a corpus path, which breaks the "13 paths
unchanged" invariant this design has held to throughout. I would rather carry one documented exception
than start editing the specification.

**Q3. Is `$type` opaque, read only through components — or does it have member access?** (§2.3)

*Recommend: opaque.* `t.size` is a member access, not a component call, so member access puts layout
reads **outside `#[use(...)]` entirely** and makes `#[use(compiler.components.layout)]` undeclarable and
unenforceable. The most safety-critical part of the API would sit outside the mechanism built to govern
it. Cost: `enums.fin:21`'s `enum_member._keyid` becomes `compiler.enums.keyid_of(m)` — a one-line stdlib
edit.
*If member access:* nicer to write, and grants stop covering the layout surface.

**Q4. How far does the interpretability line expand, and is the no-hang guarantee being spent?**

The measured line is five statement forms, no control flow, `==` as the only operator (ADR 0006). D's
spec concedes "If the function goes into an infinite loop, it may cause the compiler to hang"; Fin's
line makes that **impossible by construction**. That guarantee is the currency this design spends.

*Recommend: spend almost none of it in wave 4.* The projection principle (§2.2) exists precisely so
that no handler needs a loop: the compiler folds every list into a scalar or a quote, and the collector's
real loops run at run time. Wave 4 adds **one** thing to the line — the ability to call a component
operation that returns a `quote`, and to splice it. No `if`, no loops, no arithmetic, no arrays.
*If the line widens further:* the no-hang guarantee lapses and `finc` needs a fuel/step limit and a
diagnostic for exhausting it, which is a wave-4 item nobody has costed. Zig's `@setEvalBranchQuota` is
what that looks like once you have conceded it.

**Q7. Layout is two moments — are both in the floor, and does `request_header_words` exist?**
(§1.7, §2.5, §3.2)

Terra splits layout into `__getentries` (decide, incomplete type) and `__staticinitialize` (observe,
complete type) and enforces which questions are legal in each. A design with only the observe moment
cannot let a library contribute to a layout; with only the decide moment it cannot measure the result.

*Recommend: both moments named, both in the floor, with phase legality enforced by diagnostic.* A layout
query in the decide phase must name the phase and refuse — never return zero. That is the D interface-bitmap
defect (§1.3): an answer that is confidently wrong is worse than one that is refused.
*On `request_header_words`:* recommend **yes but marked speculative** — ADR 0003's collector has no other
way to obtain a per-object mark bit, and making it additive across handlers means two collectors get two
headers instead of a conflict. *If no:* a Fin collector must store mark state in a side table keyed by
address, which is a real design constraint on ADR 0003 and should be recorded there deliberately rather
than discovered later.

### Round 2 — shapes the design but wave 4 can start without it

**Q5. Keep `compiler.code.lift_to_module_end`, or defer it?** (§2.5, §3.4)

*Recommend: defer it.* This reverses my earlier position, and Q1 is why: with providers accepted,
`lift_to_module_end` **has no first-party consumer left**. Amending ADR 0007 for a facility the
motivating example no longer needs is the wrong trade. Racket's
`syntax-local-lift-module-end-declaration` is good prior art and this will likely come back later, with
a consumer attached.
*If kept:* needs the ADR 0007 amendment now, and the amendment is hard to scope — "declarations only, at
the end of the declaring module" is the narrowest version that is still useful.

**Q6. Is `compiler.code.ident(name)` permitted — an unhygienic escape hatch?** (§2.5)

Injected code that must name a user symbol (`CortexGC::record`) needs some way to produce an identifier
that resolves in the user's scope rather than the handler's.

*Recommend: yes, but only for **module-qualified** paths, plus `compiler.code.fresh()` for everything
else.* That covers the collector's real need — calling its own exported function — without letting a
handler capture or shadow a local it cannot see.
*If fully unhygienic:* handlers can silently capture user locals, which is the failure mode Rust's
`Span::mixed_site` exists to prevent, and it is very hard to withdraw once libraries depend on it.
*If fully hygienic with no escape hatch:* the collector cannot name its own runtime entry point and
needs a separate mechanism to do it.

**Q8. Is `protocol` adopted as the move-vs-copy mechanism?** (§3.7)

ADR 0007 states plainly that "An ownership model that wants to rewrite a move into a copy cannot be
written as an event handler." A protocol is exclusive, node-replacing, and declared
`#[protocol(<op>)]` — deliberately *not* called a handler, hook or event.

*Recommend: yes,* with slots `move_or_copy`, `deallocate`, `lifetime` (`#[slaveof]`, `variables.fin:21-34`)
and `destructor` (`deeptest2.fin:46`: "basic destructors are generated by compiler by default but you can
overwrite it" — the corpus already asks for replacement, not injection). Needs a `CONTEXT.md` entry.
*If no:* ownership stays unimplementable by a library, and ADR 0003's "memory management is a library"
holds only for collection, not for ownership.

**Q9. Is `loop_back_edge` in the event floor, or deferred?** (§3.2, §3.10)

It was deferred for lack of a consumer. *It has one:* a tracing collector needs safepoints, and a loop
with no call in it is exactly where a program can run unboundedly without reaching one.

*Recommend: in the floor.* *If deferred:* Fin's collector cannot preempt a hot loop, which rules out any
concurrent or incremental collector — a decision worth making on purpose rather than by omission.

**Q10. Confirm the handler ordering correction to ADR 0007.** (§3.5)

ADR 0007 says handlers run "in declaration order within a module and import order across modules".
**Import order is not a total order in a DAG** — with two modules each importing a third, "import order"
does not say which comes first, yet ADR 0007 also states handler order is observable and affects
behaviour. So the ADR currently makes an observable property undefined.

*Recommend:* total order = reverse post-order of the import DAG, ties broken by module path string, then
declaration order within a module. This is a **wording correction**, not a change of decision.

### Round 3 — soundness questions borrowed from other languages' documented holes

**Q11. May a `@special` branch on `compiler.system.get_*_memory`?** (§2.4)

These read the **host that is compiling**, not the target. A library branching on them emits a different
program on a different build machine, breaking the reproducibility ADR 0010 exists to guarantee — and
`memory.fin` already reads them.

*Recommend: legal to read, and a warning to branch on* — with the honest note that Fin cannot detect
"branch on" without control flow in the line (Q4), so in wave 4 this is documentation, not enforcement.
*If forbidden outright:* `memory.fin` needs rewriting. *If unrestricted and silent:* Fin has a
reproducibility hole its own stdlib demonstrates.

**Q12. What does Fin answer where D's automatic mechanisms have holes?** (§1.3)

Two documented D defects, both the same class — the automatic mechanism has a gap and the language
documents the gap instead of closing it:
- *Unions:* "a union itself never has a destructor. When a union goes out of scope, destructors for its
  fields are not called."
- *Interfaces:* `getTypePointerBitmap` sizes an interface with `t.size()` and yields a pointer-sized
  size with an all-zero bitmap — though an interface reference **is** a GC pointer.

*Recommend:* for unions, `variable_scope_exit` fires and `pointer_map_quote` of a union is a
**diagnostic**, not a zero map — the library must be told the compiler cannot answer. For interfaces, an
interface-typed field contributes exactly one pointer whose `pointee_type_at` is the `$interface`. Both
need a `//@` expectation in the corpus, or they will be discovered by a user rather than a test.

**Q13. May a `@special` be an interface method?**

D forbids `@__ctfe` on virtual methods "due to interactions with inheritance".

*Recommend: no,* and for the same reason: dispatch chosen at run time cannot select a function that only
exists at compile time. Worth stating in the same terms rather than leaving it to be discovered.
Relatedly, Fin needs **no** equivalent of D's `__ctfe` pseudo-variable, because `@special` is a distinct
function kind rather than a mode of an ordinary function — a genuine simplification over D, named as one.

**Q14. What is Fin's composition rule for scope-exit cleanup?** (§1.3)

D gets most of its use-site-free behaviour from a plain language rule, not metaprogramming: a struct
field whose type has a destructor gets it called from the parent's destructor, and "**if there is no
parent destructor, the compiler will generate one**". `__traits(needsDestruction, T)` lets a library ask.

*Recommend:* the same rule, plus `compiler.structs.has_destructor` (already on `StructType.hpp:32`) so a
handler can ask whether cleanup already runs. This matters because **it decides how much the event system
has to carry**: with composition, `variable_scope_exit` fires once for the outer variable and the nested
cleanup is the language's job; without it, the handler must walk the whole field tree itself, which needs
loops and breaks Q4.

---

## 5. The wave-4 implementation plan

Ordered so that each step is testable when it lands and nothing waits on a decision that has not been
made. **Steps 1–4 are prerequisites that are not the compiler API at all** — they are the substrate
§0.2 measured as absent, and no API work can be tested before them.

| # | Step | Why it is here | Blocked on |
| --- | --- | --- | --- |
| 1 | **Turbofish on a dotted path** — `compiler.structs.select_field::<int>(...)` | `parser.y:1416` accepts turbofish only after a bare `IDENTIFIER`, so `types.fin:23` is a **syntax error today**. It is **missing from wave 2's list in `docs/plan.md`** and wave 4 cannot proceed without it. | nothing — pure grammar |
| 2 | **`@f(...)` as an expression** | `AT` appears in four grammar places, all declaration headers, so **no `@special` can be called at all**. `@CortexCollectorInit()` — the motivating one-liner — is unparseable. | nothing — pure grammar |
| 3 | **The other three meta-types** — `$struct`, `$interface`, `$enum_member` | Only `$type` has a production (`parser.y:961`). All four are distinct types per `CONTEXT.md`, and `gettype` returns `$struct`. | nothing — pure grammar |
| 4 | **Register the meta-types in the analyzer** | `$type` parses and resolves to nothing: `Undefined type '$type'`. | 3 |
| 5 | **Ordered fields on the semantic type** | `StructType::fields` is an `unordered_map` (`StructType.hpp:28`) — declaration order is preserved in the AST and thrown away by the type. Every layout answer depends on order. | nothing |
| 6 | **The layout pass** — size, alignment, offsets | **Zero occurrences** of `offset`, `getSize`, `alignment` or `layout` in all of `src/types/` and `src/semantics/`. This is the single largest item in wave 4 and `pointer_offsets` is non-negotiable (ADR 0003). | 5 |
| 7 | **Two-phase layout** — `deciding` then `finalised`, with phase-legal queries | §2.5, Q7. Doing this *with* step 6 is far cheaper than retrofitting it, and retrofitting is how you get D's all-zero interface bitmap. | 6, Q7 |
| 8 | **Pointer maps, typed** — `pointer_count`, `pointer_offset_at`, `pointee_type_at`, `pointer_map_quote` | The surpassing capability (§1.9). Cost counted in **fields, not bytes** — a pointer-free type returns a constant-size answer, an `N`-element array contributes one element map plus a stride. | 6, 7 |
| 9 | **The component registry and `#[use(...)]` enforcement** | Makes grants mean something. `compiler.components.<name>.present()` must return `false` for an absent component rather than fail to resolve, or forward compatibility is unimplementable (§2.1a). | 4 |
| 10 | **Tier 1 components** — `types`, `structs`, `enums`, `system` | All 13 corpus paths, unchanged. This is the point at which the existing stdlib compiles. | 4, 5, 9 |
| 11 | **Invert quote analysis** | `Analyzer_Expr.cpp:541-544` analyses a quote's body as **live code** and types it `auto`. A quote is data; analysing its body is wrong and will reject every useful handler. | nothing |
| 12 | **`$name` unquote as a real node** | `parser.y:1409` produces `Identifier("$" + name)` — a string hack. Splicing needs a node. | 11 |
| 13 | **`compiler.code.*`** — `splice`, `concat`, `lit_*`, `ident`, `fresh`, `empty` | `empty()` first: "inject nothing" is the shape the most important customer takes (ADR 0007). | 11, 12, Q6 |
| 14 | **Attribute machinery** | `Attribute::accept` is empty, there is no `visit(Attribute&)`, and only `ASTPrinter` reads them. **Nothing in this design is checkable until this lands.** Coordinator's item, listed here because it gates 15. | nothing |
| 15 | **`#[on(...)]` collection and the three-phase model** — Collect / Arm / Analyse | The distinct Arm phase is what prevents Nim's "destructor called here before it was seen in this module" (§1.5). `compiler.events.enable` legal only at top level. | 14 |
| 16 | **`#[provides(...)]` and the provider mechanism** | Per-type, memoised, compiler stores and emits. Ships `type_metadata` only. | 8, 15, Q1 |
| 17 | **The event floor** — in dependency order: `struct_layout_finalised`, `variable_declared`, `variable_scope_exit`, `function_entry`/`exit`, `assignment`, `allocation_site`, `delete_site` | `variable_scope_exit` needs the `moved` analysis, including `MovedMaybe`; that is the hardest single event and should not be first. | 15 |
| 18 | **`compiler.scopes.live_pointers_quote`** | Roots. The projection that makes a shadow stack expressible without loops. | 8, 17 |
| 19 | **Diagnostics and attribution** — `compiler.diag.*`, and every injected node attributed to its handler | Rust's derive diagnostics — an error in code the user never wrote — is the ergonomic failure most likely to be repeated (§1.9). Attribution is a requirement, not a nicety. | 13, 17 |
| 20 | **`loop_back_edge`**, if Q9 says floor | Safepoints. Last because it is the only floor event with no other consumer. | 17, Q9 |

**Deferred out of wave 4, deliberately:** every value-form list operation (needs arrays and loops, Q4);
`lift_to_module_end` (no consumer once providers exist, Q5); `protocol` and everything under it (Q8 —
this is wave 5 and it is large); `compiler.source`, `compiler.modules`, `compiler.generics`,
`compiler.interp` (Tier 4, speculative); `generic_instantiated`, `field_access`, `cast`,
`import_resolved` (no consumer).

**The one thing to build first if only one thing gets built:** step 6, the layout pass. Thirteen of the
seventeen components have a consumer today, but the layout component is the only one a ratified ADR
cannot be honoured without, and it is the only one with no substrate whatsoever.

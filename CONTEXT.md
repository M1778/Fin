# Fin

Fin is a systems programming language. This repository holds the compiler (`finc`); the
package manager and build tool (`finn`) lives in a separate repository.

The language has no prose specification. `tests/samples/*.fin` is the specification, but not
uniformly: each sample is either normative or aspirational, and only a normative sample can
convict the compiler.

## Language

**blame**:
A statement that either asserts a condition or raises an error value, depending on what it
is given. `blame x > 0;` and `blame x > 0, "message";` assert; `blame SomeError("...");`
raises.
_Avoid_: assert, throw, panic — Fin has one keyword for both jobs, and calling it by either
half's name hides the other.

**m1778**:
An expression meaning "not implemented". Reaching it is a compile-time or run-time failure
depending on context, and it is expected to be eliminated as dead code when unreachable.
Written idiomatically as `blame m1778;`.
_Avoid_: unimplemented, todo, unreachable.

**Compiler component**:
A part of the compiler's own machinery, exposed to Fin code as a callable library at compile
time. Components are how the language extends itself: memory management, ownership, and
reflection are written in Fin against components rather than built into the compiler.
_Avoid_: reflection API, plugin, intrinsic, builtin — a component is ordinary Fin code calling
into the compiler, not a hole punched through it.

**Component reference**:
A component named as a value rather than used, written `compiler.components.<name>`. It is what a
grant names, and what a question about a component is asked of. Distinct from the component's
operations, which are reached as `compiler.<name>.<member>`: a reference is *about* a component, an
operation goes *through* one.
_Avoid_: component handle, component object.

**Grant**:
A declaration that a function may reach a named component, written `#[use(...)]` above it. Reaching
a component the enclosing function did not name is an error; asking whether a component exists never
needs one, or no library could check before it depended.
_Avoid_: import, capability, permission — a grant is narrower than an import and is per-function.

**Known component name**:
A component name the compiler recognises, whether or not this build ships that component. The set of
them is append-only: a retired component keeps its name so that asking about it still answers "absent"
instead of "unknown". A name outside the set is a misspelling, and it is an error where it is used —
which is the reason absent and unknown are not the same state.
_Avoid_: registered, valid, supported — and never describe an unknown name as "unavailable", because
that is precisely the confusion this distinction exists to prevent.

**Prototype**:
The builtin structural map type, written `{K,V}` as a type and `{"a":1}` as a value. Structural, so
two prototypes with the same key and value types are the same type, and it carries no ordering or
hashing guarantee — those belong to a nominal library map that a prototype converts into explicitly.
_Avoid_: dict, map, record, and above all avoid using this word for the abandoned Python
implementation in `pyprototype/`, which shares the name and is not evidence of anything.

**Special function**:
A function that runs inside the compiler while the program is being compiled, written `@special`.
It is the only place a compiler component can be reached. Ordinary code calls one with an `@` on the
call — `@_resolve_type(v)`, `@getenumkeyid(value)` — so a compile-time call is visible where it is
written rather than inferred from the callee.
_Avoid_: macro, comptime function — a macro rewrites syntax, a special function executes. Avoid
"macro invocation" for the `@name(...)` form for the same reason.

**Meta-type**:
A type being used as a value. There are four, and they are distinct: `$type` for any type, `$struct`
for a struct type, `$interface` for an interface type, `$enum_member` for one member of an enum. A
special function may take one as a parameter and return one. Because they are distinct, a function
asking about a struct and an interface has the order of its arguments checked.
_Avoid_: type object, metatype as one thing — the singular hides that there are four.

**Erasure marker**:
A generic constraint whose presence switches that generic from being compiled once per concrete
type to being compiled once for all of them. `Castable` and `Any` are erasure markers; a bare
type parameter has none.
_Avoid_: dynamic generic, boxed generic.

**Event**:
A moment during compilation that a library can be told about, such as a variable leaving scope. A
library declares interest with `#[on(...)]` on a `@special` function and arms it with a call, so
importing the library is not enough to make it act.
_Avoid_: hook, callback, trigger.

**Handler**:
The `@special` function that runs when an event fires. It may read any part of the program and may
write only code injected at the event point, returned as a quote.
_Avoid_: listener, observer, plugin.

**Protocol**:
A compiler operation that a library takes over, declared `#[protocol(<slot>)]` on a `@special`. Exactly
one library may claim a slot, and the claimant *replaces* the operation instead of adding to it — which is
why it is not a handler: a handler injects at a point it did not choose, a protocol answers for the whole
operation. Slots include `move_or_copy`, `deallocate`, `lifetime` and `destructor`.
_Avoid_: hook, override, handler — a handler is precisely the mechanism that cannot replace anything.

**Provider**:
A `@special` the compiler calls once per subject, whose returned value the compiler stores and emits,
declared `#[provides(<slot>)]`. The library writes nothing: it answers a question and the compiler does
the writing. Exclusive per slot and memoised per subject, so a provider has to be a pure function of its
subject — the compiler may call it once and reuse the answer, and may not call it at all when nothing
observes the answer.
_Avoid_: generator, callback, hook — and never handler, which writes at a program point.

**Layout moment**:
One of the two phases in which a type's layout is settled. In *decide* the type is incomplete and a
library may contribute header words; in *observe* it is complete and the layout may be measured. A query
belonging to the other moment refuses and names the moment rather than returning a value.
_Avoid_: layout pass, layout phase — "moment" because there are exactly two of them and both are named.

**Nullable**:
A declaration marked with `?`, meaning the thing it names may hold or return nothing. Written on a
member (`b? <int>`), a parameter (`n?: int`), a binding (`let x? <A>`), or a function whose return
may be absent (`fun? make_A(...)`).
_Avoid_: optional, maybe — and in particular avoid calling this the *nullifier*, which is what the
corpus calls it, because that name is one letter from its opposite.

**Denullify**:
To read a nullable value as its underlying type, failing if it is absent. Written as a postfix `?`
on an expression: `make_A(1)?`. It is the inverse of a nullable declaration, and shares the symbol
with it and with the conditional expression's `otherwise` arm.
_Avoid_: unwrap, force-unwrap.

**readonly**:
A member that only its declaring type may write. Its first initialisation from outside is allowed;
every later write from outside is a compile error, not a raised error value.
_Avoid_: const, immutable, final — `const` in Fin is a separate thing, a binding whose value is
fixed at compile time.

**Visibility label**:
A `pub:` or `priv:` line inside a type body that sets the default visibility for the members after
it, until the next label. A `pub` or `priv` written directly on one member overrides the enclosing
label for that member alone.
_Avoid_: access specifier, section.

**Constraint set**:
A set of types admissible where a bound is expected, written `type Number = int | uint | float;`. It bounds a
generic and never stores a value, so a binding or parameter declared with one is an error. Distinct from a
type alias, which names one type and does store values, and told apart from it only by the alternation.
_Avoid_: union, sum type, variant — Fin's sum type is a generic `enum`, and a constraint set is the thing that
deliberately is not one.

**Host value**:
A compile-time value read from the machine running the compiler, such as its available memory. It may be
returned, stored and formatted, but branching on one at compile time is refused, so that the same source
always compiles to the same program. The restriction travels with the value through `@special` calls.
_Avoid_: environment value, platform value, tainted value — the last describes the mechanism rather than the
thing, and the mechanism may change.

**Interface reference**:
An interface used as a runtime type: two words, a data pointer and a vtable pointer. It has the same shape as
`any` but not the same second word, so the two are not interchangeable and converting between them is a real
conversion.
_Avoid_: trait object, boxed interface, and bare "fat pointer" — an erased generic is one of those too.

**Pointer map**:
The per-type answer saying what each word of a type is: traced, not a pointer, or a pointer the collector must
not follow. A provider supplies it and the compiler stores and emits it. The third state exists because a
vtable pointer is a real pointer into static data, and calling it either of the other two is wrong.
_Avoid_: bitmap, pointer mask — both imply one bit per word, which three states rules out.

**Fresh identifier**:
An identifier minted by the compiler for injected code, guaranteed to collide with nothing. Injected code
binds only fresh identifiers and may spell only module-qualified paths, so it can neither shadow a local nor
capture one. How a fresh identifier is spelled is not stable between compilations and nothing may depend on
it.
_Avoid_: gensym, hygienic name, mangled name.

**Sample**:
A `.fin` file under `tests/samples/` that defines part of the language by example. Every sample
is either normative or aspirational; a sample carries no weight until it says which.
_Avoid_: fixture, test case, example.

**Normative sample**:
A sample that states what the language already is. When the compiler disagrees with one, the
compiler is wrong, and the sample changes only by a deliberate language decision.

**Aspirational sample**:
A sample that states what the language is intended to become. It is a design sketch, so the
compiler failing on one is expected and reports nothing about the compiler's correctness.
_Avoid_: WIP, TODO, draft — those describe the file's tidiness, not its authority.

**Expectation**:
A `//@` line in a sample saying what the compiler should do with it: `ok`, `error <line>:<col>
"<msg>"`, or `unimplemented "<reason>"`. Authority lives here rather than in the file's label, so one
construct inside a normative sample can be excused, and one inside an aspirational sample can be held
to account. A sample with no expectation is a fault in the harness, not a passing test.
_Avoid_: assertion, directive, pragma.

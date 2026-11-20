# Diagnostic codes

`finc` names its diagnostics with stable codes. A code identifies a *rule*, not one
message: `E0201` is every type mismatch, whatever the two types are, and the message on
the diagnostic names the specifics.

```
$ finc --explain E0201
$ finc --explain list
```

`--explain <code>` prints the long form below on stdout; `--explain list` prints the
inventory, sorted by code. Neither needs a source file. An unknown code is a
command-line error (exit `2`) naming it — a code this compiler does not raise is usually
a version skew between `finc` and whatever printed the code. See
[the interface contract](finc-interface-contract.md#explaining-diagnostics).

This page is generated from the same table the compiler assigns codes from, so it cannot
list a code nothing raises or omit one that something does:
`tests/test_diagnostic_help.cpp` checks the inventory against this file and compiles every
example below to prove it still reproduces its own code.

## How the numbers are laid out

The hundreds digit names the pass that owns the rule, so a code says where to look before
you read its message. `E0101` and `E0201` were published before the scheme and keep their
numbers; everything else was appended around them.

| Range | Family | What it covers |
| --- | --- | --- |
| `E01xx` | Backend | Code generation. The front end accepted the program and lowering could not proceed. |
| `E02xx` | Types | A value's type does not fit the position the program puts it in. |
| `E03xx` | Names | A name, member, or method that does not resolve where it was written. |
| `E04xx` | Declarations | A declaration that cannot stand as written: generic arity, conformance, layout, attributes. |
| `E05xx` | Operations | An operator, statement, or pattern the operand types cannot support. |
| `E06xx` | Modules | The loader could not find, load, or satisfy an import. |
| `E07xx` | Ownership | A reference that would outlive the storage it names. |
| `E08xx` | Invocation | Reserved: a diagnostic about the command line rather than a place in a file. *(reserved, no codes yet)* |
| `E09xx` | Lexer and syntax | Reserved: source that is not a program. |

## The inventory

| Code | Severity | Meaning |
| --- | --- | --- |
| `E0101` | error | The backend cannot lower the construct, and says so instead of guessing |
| `E0201` | error | A value's type is not assignable to the type its position requires |
| `E0301` | error | A name was used that nothing in scope declares |
| `E0302` | error | The named member does not exist on the type the expression was written against |
| `E0303` | error | A method or static method was called that the receiver's type does not declare |
| `E0304` | error | A private method or field was used from outside the type that declares it |
| `E0305` | error | A private field was set from a struct literal written outside its type |
| `E0401` | error | A generic type or function was written with the wrong number of type arguments |
| `E0402` | error | A type claimed a conformance it does not satisfy, or a bound was given a type outside it |
| `E0403` | error | A function that declares a result type can reach its end without returning one |
| `E0404` | error | `Self` was written somewhere that has no enclosing struct to stand for |
| `E0405` | error | An attribute name is not one the compiler implements |
| `E0406` | error | A struct field's type is a union, which has no single layout to store |
| `E0501` | error | A `const` binding was written after its declaration |
| `E0502` | error | A `readonly` field was assigned outside the type that declares it |
| `E0503` | error | A value was `delete`d that names no storage the program allocated |
| `E0504` | error | A subscript was written with a non-integer index |
| `E0505` | error | A constant array index is negative |
| `E0506` | error | A constant array index is past the array's known extent |
| `E0507` | error | An arithmetic operator was given operands that are not numbers |
| `E0508` | error | A shift was written with a count outside the shifted type's width |
| `E0509` | error | An explicit conversion was written between two types that have none |
| `E0510` | error | `break` or `continue` was written where there is no loop to leave |
| `E0511` | error | A `match` expression does not cover every value the scrutinee can take |
| `E0512` | error | A match arm can never be selected because an earlier arm already covers it |
| `E0513` | error | Two match arms name the same member, constant, or `null` |
| `E0514` | error | The `*` prefix was applied to a value that is not a pointer |
| `E0601` | error | An import named a module no search path holds |
| `E0602` | error | An import named a symbol the module exists but does not provide |
| `E0603` | error | An import named a namespace inside a module that does not declare it |
| `E0604` | error | An import could not be satisfied, and this is the located half of the reason |
| `E0701` | error | A frame-local's address would be used after the frame that owns it ends |
| `E0901` | error | The lexer met a byte that begins no token |
| `E0902` | error | A statement or declaration ended without the semicolon that ends it |

## `E0101` — the backend cannot lower the construct, and says so instead of guessing

Message: codegen:<what> is not lowered yet

The front end accepted the program, but the LLVM backend has no lowering for one
construct in it. The message names that construct; the rule behind the refusal is
that an unsupported runtime operation is reported, never silently dropped or
emitted as guessed IR (AGENTS.md, Backend invariant).

Some refusals are permanent rulings rather than gaps (struct '==' is
declared-only, ADR 0036), and some are known gaps with an ADR tracking them. A
refusal names the construct so the next reader can tell which this is. Checking a
program without '-c' or '-o' never reaches lowering, so a clean frontend check
does not rule this code out; compile an object to see it.

Example:

```fin build-error
fun make<T>() <T> { return 1; }
fun main() <noret> { let x <int> = make(); }
```

Rule: AGENTS.md, Backend invariant; each refusal names the gap it belongs to

## `E0201` — a value's type is not assignable to the type its position requires

Message: Type mismatch:<what>

Semantic analysis refused a value because it cannot stand where the program puts
it: an initializer against its annotation, an argument against its parameter, an
operand against the other operand or the declared result. The message spells the
wanted type first and the type found second: expected 'int', got 'string'.

Integers widen implicitly and never narrow (ADR 0022), so 'int' into 'long' is
accepted while 'long' into 'int' is this code. A negative constant is never an
unsigned value, so 'let x <ulong> = -1' is refused even though the width fits.
Fin has no cast syntax ruled on yet, so a genuine narrowing has no spelling.
One shape carries its own help: 'object' into 'any' is refused with
`cast<any>(x)` named, because erasure cannot read a runtime type back.

Example:

```fin error
fun main() <noret> { let x <int> = "s"; }
```

Rule: ADR 0022 (integer widening); samples under tests/samples/ are the corpus

## `E0301` — a name was used that nothing in scope declares

Message: Undefined<what>

Semantic analysis reached a name and found no declaration for it: a local, a
parameter, a type, a struct, or a function. The message says which kind it
looked for and quotes the spelling as written, so `Undefined variable 'x'` and
`Undefined type 'x'` are one rule with two questions rather than two rules.

Resolution is lexical and, for a name used before its declaration in the same
block, order-dependent where the corpus proves it must be. An import binds only
what it names: a whole-module import does not bring a macro into scope, and a
named import carries exactly its names. A typo in a keyword reaches this code
only when it names no declaration; the lexer's own typo hint goes in `help`.

Fix it by declaring the name, importing it, or correcting the spelling. The
`help` key carries a `did you mean` guess when the misspelling is close enough to
a keyword to suggest one, which is a hint and never part of the code.

Example:

```fin error
fun main() <noret> { let x <int> = nosuch; }
```

Rule: docs/guide/04-functions-and-scopes.md; samples under tests/samples/

## `E0302` — the named member does not exist on the type the expression was written against

Message: <what> has no member '

A member access named a field, enum member, or positional member the receiver's
type does not have. The message names the receiver's type and the member as
written, so the same rule covers `Struct 'S' has no member 'b'`,
`Enum 'E' has no member 'C'`, and the positional-member forms.

A struct's fields are the ones its body declares, plus any a parent contributes
(ADR 0026); an enum's members are the enumerators its body lists (ADR 0041).
Members are looked up on the type, not on the value, so a name that exists on a
different type with the same field names is still this code.

Fix it by writing a member the type declares, or by declaring the member. A
member that exists but is not public is a different code (E0304), and a method
that does not exist on a type that has methods is E0303.

Example:

```fin error
struct S {
    a <int>
}
fun main() <noret> { let s <S> = S { a: 1 }; let y <int> = s.b; }
```

Rule: docs/guide/06-structs-and-classes.md; ADR 0026, ADR 0041

## `E0303` — a method or static method was called that the receiver's type does not declare

Message: <what> not found in 

A call was written in method position — `x.m()` or `T::m()` — and the receiver's
type declares no method by that name. The message names the method and the type,
which is the whole rule: method lookup is on the receiver's type and its
ancestry, and a free function of the same name is not a method.

This is separate from E0302 because the two questions have different answers. A
missing field is a misspelt or undeclared member; a missing method is usually a
call that should have been a free function, or a method on another type. The
message distinguishes a dot call (`not found in type`) from a `::` call (`not
found in`), because a static lookup and a receiver lookup search different
tables.

Fix it by declaring the method on the type, spelling the method that exists, or
calling the free function by name.

Example:

```fin error
struct S {
    a <int>
}
fun main() <noret> { let s <S> = S { a: 1 }; let y <int> = s.nope(); }
```

Rule: docs/guide/06-structs-and-classes.md; ADR 0027 (interface method dispatch)

## `E0304` — a private method or field was used from outside the type that declares it

Message: Cannot access private<what>

A `priv:` label (or an unlabelled member under the default visibility) makes a
member reachable only from inside its own type's body. This diagnostic is that
rule being enforced: the member exists, and the access is from somewhere else.

The message says whether it was a method or a field, and names both the member
and the struct. `priv:` and `pub:` are visibility labels that set the default for
the members after them (CONTEXT.md), so a member is private by position as well
as by spelling.

Fix it by marking the member `pub`, or by moving the access inside the type. A
private field written in a struct literal is E0305 rather than this code, because
initialising a field and reading one are different operations.

Example:

```fin error
struct S {
    priv: fun secret() <int> { return 1; }
}
fun main() <noret> { let s <S> = S { }; let y <int> = s.secret(); }
```

Rule: CONTEXT.md, visibility labels; docs/guide/06-structs-and-classes.md

## `E0305` — a private field was set from a struct literal written outside its type

Message: Cannot initialise private<what>

Struct-literal initialisation writes fields directly, and a private field cannot
be written from outside the type that declares it. This is the write half of the
visibility rule that E0304 enforces on reads: the field exists, the caller may
not fill it in.

The type's own constructors and methods can initialise it, which is what makes a
private field meaningful: outside code constructs through the type's own
construction rather than by writing the field. Inside the type's body the same
literal is accepted.

Fix it by marking the field `pub`, by constructing through a constructor the type
declares, or by moving the literal inside the type's own scope.

Example:

```fin error
struct S {
    priv: a <int>
}
fun main() <noret> { let s <S> = S { a: 1 }; }
```

Rule: CONTEXT.md, visibility labels; docs/guide/06-structs-and-classes.md

## `E0401` — a generic type or function was written with the wrong number of type arguments

Message: Generic count mismatch<what>

A declaration says how many generic parameters it takes; a use wrote a different
number in `::<...>`. The message spells the declared count and the written count,
because the fix is one of the two numbers and nothing else.

The bare `Generic count mismatch` form is the same rule at a site that has no
count to print — an instantiation reached through a path that does not carry the
written list. Treat it the same way: the arity at the use does not match the
declaration.

Fix it by writing exactly the declared number of arguments, or by omitting the
list where inference can supply it. `Box<int>` for a `Box<T>` is right;
`Box<int, string>` is this code.

Example:

```fin error
struct Box<T> {
    val <T>
}
fun main() <noret> { let b <Box<int>> = Box::<int, string> { val: 1 }; }
```

Rule: docs/guide/09-generics.md

## `E0402` — a type claimed a conformance it does not satisfy, or a bound was given a type outside it

Message: <what> does not implement

Fin has two conformance questions and this code answers both. An `implements`
block or a struct's parent list claims an interface, and the type is missing a
member the interface requires (ADR 0027). A generic bound `T: I` is satisfied only
by a type that implements `I` or by a memberless mock, and a primitive argument to
a member-full bound is refused here too.

The same predicate decides both, so an `S<int>` instantiation and an assignment
into a bound agree instead of each having half the rule. A struct-vs-interface
question reads `does not implement interface`; a bound against a non-interface
constraint reads `does not implement`.

Fix it by implementing the missing members, by declaring the type as implementing
the interface, or by passing a type that satisfies the bound.

Example:

```fin error
interface I {
    pub fun f() <int>;
}
struct S: <I> { }
fun main() <noret> { let s <S> = S { }; }
```

Rule: ADR 0027; docs/guide/10-interfaces.md

## `E0403` — a function that declares a result type can reach its end without returning one

Message: Function '<what> is missing a return statement

A function whose declared result is not `noret` or `void` must return a value on
every path that reaches the end of its body. This diagnostic names the function
and says the gap is on *some* paths, because a body that never falls through is
accepted and one that can is not: `if c { return 1; }` alone is this code, while
`if c { return 1; } else { return 2; }` is not.

Fix it by returning on the missing path, by making the last statement an
unconditional `return`, or by declaring the function `noret` if it genuinely
never produces a value.

Example:

```fin error
fun f() <int> { }
fun main() <noret> { let x <int> = f(); }
```

Rule: docs/guide/04-functions-and-scopes.md

## `E0404` — `Self` was written somewhere that has no enclosing struct to stand for

Message: 'Self' used outside of struct<what>

`Self` is the name a struct's body uses for the type being declared. Outside such
a body there is nothing for it to mean, and this diagnostic says so by name. It
is an error rather than a guessed type because guessing would bind `Self` to
whatever type the call site happened to construct.

The same rule covers `Self(...)` as a constructor call: inside a struct body it
constructs that struct, and outside one there is no `Self` to construct.

Fix it by naming the concrete type, or by moving the code inside the struct whose
`Self` it means.

Example:

```fin error
fun main() <noret> { let x <int> = Self(); }
```

Rule: docs/guide/06-structs-and-classes.md

## `E0405` — an attribute name is not one the compiler implements

Message: Unknown attribute '<what>

Attributes are `#[name]` markers the compiler reads, so a name it does not
implement cannot be ignored: an ignored attribute is a program that silently
does not get the behaviour it asked for. The message quotes the name.

The known set is the compiler's own. `pub:`/`priv:` are visibility labels rather
than attributes; the declarative attributes the compiler acts on include
`global`, `class`, `export`, `overwrite`, `implements`, `provides`, `protocol`,
`slaveof`, `on`, `use`, `attribute`, `type`, `RT`, `future` and the `std`-scoped
spellings. A compile-time library cannot register one yet: wave 4 is where
`compiler.grammar.declare` gives a library an attribute of its own
(docs/compiler-api.md, Q2).

Fix it by spelling an attribute that exists. The authoritative list is
`isReaderOwnedAttribute` in src/semantics/impl/Analyzer_CompilerApi.cpp, together
with the parser's attribute rules.

Example:

```fin error
#[nosuchattr]
fun g() <noret> { }
fun main() <noret> { }
```

Rule: src/semantics/impl/Analyzer_CompilerApi.cpp

## `E0406` — a struct field's type is a union, which has no single layout to store

Message: <what> has union type '

A union type `A | B | C` describes a value that may be any one of its members,
and which one is known only at run time. A struct field needs a fixed layout, so
a union-typed field is refused at declaration time rather than given the layout
of its first member (ADR 0015, docs/compiler-api.md Q12).

The message names the field and the union alias. This is not a missing lowering:
the pointer map of a union is a diagnostic by ruling, and a silent zero map would
be a wrong answer instead of a refusal.

Fix it by storing a concrete member type, by using an enum with a payload (ADR
0041), or by using an erasure alias such as `Offer` where a dynamic choice is
genuinely wanted.

Example:

```fin error
type Number = int | uint | float;
struct S { pub v <Number>, }
fun main() <noret> { }
```

Rule: ADR 0015; docs/compiler-api.md Q12; tests/samples/union_pointer_map.fin

## `E0501` — a `const` binding was written after its declaration

Message: Cannot<what> immutable variable '

`const` declares a binding that is never written again, and `let` declares one
that may be. This diagnostic is a write to the former, either through assignment
(`x = 2`) or through an increment or decrement (`x++`), which are writes with
operator spelling.

The message names the variable and the verb, so the two spellings are one rule:
a `const` is immutable for the whole of its lifetime, not only outside the scope
that declared it.

Fix it by declaring the binding `let`, or by removing the write. A `readonly`
field is a different restriction (E0502) because it is about a field of a value
rather than a binding's name.

Example:

```fin error
fun main() <noret> { const x <int> = 1; x = 2; }
```

Rule: docs/guide/02-variables-and-types.md

## `E0502` — a `readonly` field was assigned outside the type that declares it

Message: Cannot assign to readonly field '<what>

`readonly` is a static property of a field: the declaring type's own methods may
initialise or update it, and every other write is refused before code generation.
That is what makes it a rule rather than a runtime check — a try/catch cannot
turn an avoidable violation into a valid program.

The message names the field and the struct. The rule is scoped to the declaring
type, not to the file: the same assignment inside the type's body is accepted.
ADR 0045's `restrict` is the value-level snapshot of the same idea and is a
different mechanism.

Fix it by removing the assignment, by marking the field writable, or by moving
the write into the type's own methods.

Example:

```fin error
struct S {
    readonly a <int>
}
fun main() <noret> { let s <S> = S { a: 1 }; s.a = 2; }
```

Rule: CONTEXT.md, readonly member; ADR 0045

## `E0503` — a value was `delete`d that names no storage the program allocated

Message: Cannot delete non-pointer type<what>

`delete` frees storage a pointer names, so its operand has to be a pointer — or a
dynamic array `[T]`, which is what `new [T, n]` produces and which
`delete self._arr` frees in `lib/std/collection.fin`. A value of any other type
names storage `delete` cannot free, and a *fixed*-extent array `[T, n]` is
refused because an annotation and an array literal both produce one and neither
came from an allocator.

The message quotes the operand's type. One thing a type cannot say is
provenance, so a `[T]` that decayed from a literal is accepted here; that is the
same latitude `delete p` has over a pointer to a local.

Fix it by deleting the pointer or dynamic array that owns the allocation, or by
removing the `delete` and letting scope exit release the value (ADR 0030).

Example:

```fin error
fun main() <noret> { let x <int> = 1; delete x; }
```

Rule: ADR 0003 (memory management is a library); ADR 0030

## `E0504` — a subscript was written with a non-integer index

Message: An index must be an integer<what>

Arrays and pointers are addressed by an integer position, so a subscript written
with a string, a struct, or any other type has no meaning to compute. The message
quotes the index's type.

The rule is about the index's type rather than its value: a negative integer
reaches E0505 and an integer past a known extent reaches E0506, and both are
integer-index questions rather than this one.

Fix it by computing an integer position, or by indexing the collection the way
its own API supports (`get`, `try_get`).

Example:

```fin error
fun main() <noret> { let a <[int]> = [1]; let x <int> = a["s"]; }
```

Rule: ADR 0025 (a dynamic array is a pointer and a length)

## `E0505` — a constant array index is negative

Message: An array index cannot be negative<what>

An array is addressed from zero, so no length makes `-1` a valid position. This is
refused whether or not the array has a known extent, because the rule is about
the sign and not about the bound.

Only a constant index can be decided here; a run-time index is not this code,
because the compiler does not know its value. A constant index that is
non-negative but past a known extent is E0506, which quotes the extent.

Fix it by writing a non-negative index, or by guarding the position before the
subscript when it comes from a computation.

Example:

```fin error
fun main() <noret> { let a <[int, 3]> = [1, 2, 3]; let x <int> = a[-1]; }
```

Rule: ADR 0025; tests/samples/arrays.fin

## `E0506` — a constant array index is past the array's known extent

Message: <what> is out of bounds for an array

An array type written `[int, 3]` carries its length, so the compiler can decide a
constant subscript against it. This diagnostic quotes the index and the extent,
because `a[5]` is only wrong relative to a length written somewhere else. An index
too large to read as a number is reported as `that index` rather than a number no
reader could finish reading.

A run-time index is not this code: the value is unknown, so no bound can be
checked. A negative constant is E0505 and is decided before the extent is
consulted.

Fix it by writing an index inside the extent, or by widening the array's declared
length.

Example:

```fin error
fun main() <noret> { let a <[int, 3]> = [1, 2, 3]; let x <int> = a[5]; }
```

Rule: ADR 0025; tests/samples/arrays.fin

## `E0507` — an arithmetic operator was given operands that are not numbers

Message: Arithmetic operator requires numeric operands<what>

`+`, `-`, `*`, `/` and the rest compute over numbers. The message names both
operand types, because the same operator over two non-numeric types is the same
mistake whichever pair it is.

String concatenation is not `+` in Fin: string equality compares content (ADR
0049), and building a string from parts is the formatting API's job. A struct's
operator is its own declared operator and is reached by name, so a user-defined
`+` on a struct is not this code.

Fix it by giving the operator numeric operands, or by calling the operation the
types actually define.

Example:

```fin error
fun main() <noret> { let s <string> = "a"; let t <string> = "b"; let x <int> = s + t; }
```

Rule: ADR 0049; docs/guide/02-variables-and-types.md

## `E0508` — a shift was written with a count outside the shifted type's width

Message: Shift count<what> out of range

A shift of an N-bit value by N or more has no defined result in the type it is
written at, and a negative count has none at all. This diagnostic quotes the
count, the width, and the type, which are the three numbers the fix needs.

The check is on a constant count, because a run-time count's value is unknown.
The width is the shifted operand's own width, so `1 << 40` on a 32-bit `int` is
this code and the same count on a `long` is not.

Fix it by writing a count in `[0, width)`, or by widening the operand.

Example:

```fin error
fun main() <noret> { let x <int> = 1 << 40; }
```

Rule: ADR 0054 (signed integer overflow wraps); docs/guide/02-variables-and-types.md

## `E0509` — an explicit conversion was written between two types that have none

Message: Invalid cast from<what>

`cast<T>(x)` is the programmer overriding the checker, and it is not
unchecked: the pair has to be one the language rules a conversion for. The
message names the source and target types, which is the whole question.

The admitted pairs are: a type with itself; any two numeric primitives (so a
cast does narrow, unlike an assignment, which only widens per ADR 0022); a bool
and an integer; an integer and a pointer, in that direction; two pointers; a
type and a generic parameter; anything touching `any` in either direction, which
is how a value enters and leaves an erased slot; an enum and a non-string
primitive, in either direction, because an enum is an integer at the
representation level; and a value into the nullable of a type it could cast or
assign to.

What is refused is a `string` against anything that is not a string (formatting
is not a cast, #93), a struct that is not an enum, and a fixed-extent array into
an enum (`casts` from `arrays_enums.fin:23`'s commented-out line). `cast<auto>`
is accepted and means the operand's own type.

Fix it by casting through a type that has a ruled conversion, or by converting
with the API that owns the operation.

Example:

```fin error
fun main() <noret> { let s <string> = "s"; let y <int> = cast<int>(s); }
```

Rule: ADR 0022 (widening applies to assignment, not to an explicit cast); #93; ADR 0034

## `E0510` — `break` or `continue` was written where there is no loop to leave

Message: <what> used outside of loop

`break` leaves the nearest enclosing loop and `continue` starts its next
iteration. Outside a loop there is nothing either could do, so both are refused
rather than silently ignored. The message quotes the keyword written.

The check is lexical: the keyword must appear inside a loop body, decided by the
walk's own loop nesting rather than by whether a loop happens to call the code at
run time. A lambda written outside any loop is this code; one written lexically
inside a loop is a known gap in the check and is refused later by the backend
rather than here.

Fix it by moving the statement into the loop body, or by removing it.

Example:

```fin error
fun main() <noret> { break; }
```

Rule: docs/guide/05-control-flow.md

## `E0511` — a `match` expression does not cover every value the scrutinee can take

Message: non-exhaustive match<what>

A `match` is an expression, so it must produce a value for every possible
scrutinee. This diagnostic lists what is not covered: unmatched enum members, the
constants a literal pattern cannot cover, or the surviving values after the arms
written (ADR 0050).

An enum match covers its members or needs `_`; a nullable match covers `null` and
the present value; a constant match over an integer or string always needs `_`,
because literals never cover every value. The message says which of these applies
by naming what is missing.

Fix it by adding the missing arms, or by adding `_` where a catch-all is genuinely
what the program means.

Example:

```fin error
enum E { A, B }
fun main() <noret> { let e <E> = A; let n <int> = match (e) { A => 1 }; }
```

Rule: ADR 0050; tests/samples/match.fin

## `E0512` — a match arm can never be selected because an earlier arm already covers it

Message: unreachable match arm<what>

Arms are tried in order, so an arm after a `_`, after a binder that covers every
present value, or a repeated member can never run. A never-selected arm is dead
code the author did not know was dead, so it is an error rather than a warning.

The message says which earlier shape covers it — `_`, a binder, or a repeated
member — so the fix is to move the arm earlier or delete it. A duplicate is
E0513 when the repeated member is exactly the earlier one and unreachable when an
earlier catch-all is what covers it.

Fix it by deleting the arm, or by moving it before the arm that covers it.

Example:

```fin error
enum E { A, B }
fun main() <noret> { let e <E> = A; let n <int> = match (e) { _ => 1, A => 2 }; }
```

Rule: ADR 0050; tests/samples/match.fin

## `E0513` — two match arms name the same member, constant, or `null`

Message: duplicate match arm<what>

Two arms that name the same value make the second dead: the first already
selected. This is separate from E0512 because the cover is the same value rather
than a catch-all, and the fix is to remove one of the two rather than to move it.

The message names the repeated member, constant, or `null`. The rule applies per
kind of pattern — enum members, literal constants, and the nullable `null` arm —
and the message says which.

Fix it by deleting one of the two arms, or by writing the second's body into the
first.

Example:

```fin error
enum E { A, B }
fun main() <noret> { let e <E> = A; let n <int> = match (e) { A => 1, A => 2, _ => 3 }; }
```

Rule: ADR 0050; tests/samples/match.fin

## `E0514` — the `*` prefix was applied to a value that is not a pointer

Message: Cannot dereference non-pointer type<what>

`*p` reads the value a pointer names, so its operand must be a pointer. The
message quotes the operand's type. An `&T` and a `[T]`-style pointer both
dereference; a primitive, struct, or interface value is already the value and has
nothing to follow.

Fix it by dereferencing the pointer that holds the storage, or by removing the
`*` if the value was the thing wanted.

Example:

```fin error
fun main() <noret> { let x <int> = 1; let y <int> = *x; }
```

Rule: ADR 0025; docs/guide/02-variables-and-types.md

## `E0601` — an import named a module no search path holds

Message: module not found:<what>

The loader searched its paths and found no file for the import, so nothing was
compiled from it. This diagnostic is about the search, not about the file's
contents: the `help` key lists the paths searched, in order, so the reader can
see which one was expected to hold the module.

A quoted import is a path looked up beside its importer and then through the
search paths; a package import is a dotted name turned into a path and looked up
through the search paths only. The message quotes the import as written, so the
two searches stay distinguishable.

Fix it by adding the module's directory with `-I` or `--fin-libs`, or by
correcting the import's spelling. This diagnostic has no source location, because
it is about the invocation's search paths as much as about the import line.

Example:

```fin error
import { n } from "no-such-module";
fun main() <noret> { }
```

Rule: docs/finc-interface-contract.md, Library search paths; ADR 0009

## `E0602` — an import named a symbol the module exists but does not provide

Message: Module '<what> does not export

The module was found and loaded, and its scope holds no declaration by the name
the import asked for. The message names the module and the missing symbol, which
is the pair the reader needs: which file to look in, and what to look for.

A named import carries exactly the names it names, and a whole-module import
brings every name the module declares but still resolves each use. The
distinction that matters here is that the module *is* present — a module that is
absent is E0601, and a namespace tail the module never declared is E0603.

Fix it by spelling an exported name, or by adding the export to the module.

Example:

```fin error
import { nope } from error::std;
fun main() <noret> { }
```

Rule: docs/guide/11-macros-and-preprocessor.md; ADR 0033

## `E0603` — an import named a namespace inside a module that does not declare it

Message: Module '<what> has no namespace

Strict namespaces (ADR 0029-era import rule, issue #90): a `::` tail names a
namespace inside the module, and every segment written must be one the module
declares. Naming one it does not declare is an error rather than a silent no-op,
because a silent no-op binds nothing and the failure surfaces later as an
undefined name.

The message names the module and the segment that failed, so a multi-segment path
that fails deep says exactly which segment was wrong. A quoted file import
carries no tail and cannot reach this code.

Fix it by writing a namespace the module declares, or by dropping the tail.

Example:

```fin error
import { x } from error::nosuch;
fun main() <noret> { }
```

Rule: docs/guide/11-macros-and-preprocessor.md; issue #90

## `E0604` — an import could not be satisfied, and this is the located half of the reason

Message: Failed to load module<what>

The loader's own diagnostic (E0601) says why a search failed and has no source
location. This code is the import statement's own diagnostic: it points at the
line that asked, names the module as written, and is emitted so every import
failure has a place in the file beside the invocation-level report.

Both are emitted for one failure, which is why a run that cannot find a module
shows two codes: E0601 for the search and this one for the site. Neither is a
duplicate of the other — one is about paths and one is about the statement.

Fix it as for E0601: correct the search paths or the import.

Example:

```fin error
import { n } from "no-such-module";
fun main() <noret> { }
```

Rule: docs/finc-interface-contract.md, Library search paths; ADR 0009

## `E0701` — a frame-local's address would be used after the frame that owns it ends

Message: <what> local variable '

A local variable's storage belongs to the call that declared it. Returning its
address, storing it somewhere that outlives the call, or handing it out as an
interface would leave a reference to storage no longer owned — a dangling pointer
that reads whatever the frame is reused for next (ADR 0055, ADR 0056).

The message quotes the variable and distinguishes the three routes: `cannot
return the address of`, `cannot store the address of ... into storage that
outlives the call`, and `cannot return local variable ... as an interface`. The
`help` key names the fix for a return: return caller-owned storage, or an owned
heap allocation via `new`.

The rule is about storage, not about variables: a reference to a caller's value, a
static, or a heap allocation is fine, and only frame-local storage is refused.

Example:

```fin error
fun f() <&int> { let d <int> = 1; return &d; }
fun main() <noret> { }
```

Rule: ADR 0055; ADR 0056; tests/samples/pointer_escape.fin

## `E0901` — the lexer met a byte that begins no token

Message: unrecognised byte in source:<what>

The lexer's catch-all rule matched one byte and produced no token for it. The
message renders the byte as an escape (`\x01`) rather than embedding it, because
the byte may not be valid UTF-8: the catch-all matches one byte at a time, so a
multi-byte character arrives here split, and pasting it into a message would
produce a diagnostic the JSON consumer cannot parse.

This is an error and not a skip. A byte the lexer silently discarded would leave
the surrounding text to parse as whatever it happened to spell, so the failure
would surface later as an unrelated syntax error pointing at the wrong line.

Fix it by removing the byte, or by writing it inside a string literal or comment
where it is text rather than source.

Example:

```fin error
fun main() <noret> { let x <int> = 1; \ }
```

Rule: src/diagnostics/DiagnosticEngine.cpp, reportLexerError

## `E0902` — a statement or declaration ended without the semicolon that ends it

Message: expected ';'<what>

Fin ends statements with `;`, so a line that does not end in one is refused where
the next token proved the statement was over. The parser points at the end of the
broken line rather than at the token that surprised it, because the missing
semicolon is the thing to add.

This is reported separately from the general `syntax error, unexpected ...` form
because the fix is known: when `;` is among the tokens that would have been
accepted, naming it is more useful than naming every token that could follow.

Fix it by adding the `;`. A missing `;` on the last statement of a block is this
code too, and is pointed at the end of that statement's line.

Example:

```fin error
fun main() <noret> { let x <int> = 1 }
```

Rule: src/parser/parser.y, report_syntax_error (issue #44)

A diagnostic that matches no row still renders `"code":null` in
`--diagnostics=json`. That is not an omission in this page: a code is added with the rule
that raises it, and a code is never invented to fill a table.

A family is left uncoded when its message shape is too vague to key on without stealing
another family's messages — the syntax family (`syntax error, ...`, `expected ';'`) and
the lexer's catch-all are the current examples: their text is shared with parser internals
that are not one rule. A row that mis-matches is worse than no row, so those stay `null`
until a predicate exists that is both specific and complete.

## Adding a code

A new code is a new row in `kCodeRegistry`
(`src/diagnostics/DiagnosticEngine.cpp`), which is both the matcher that stamps a code onto
an emitted message and the table `--explain` answers from. One row carries the code, the
message shape it matches, its severity, this page's wording, an example, and the ruling
that owns it; so a code cannot be raised without an explanation, and an explanation cannot
describe a code nothing raises. Regenerate this page from that table:

```sh
uv run --no-project tests/tools/gen_diagnostics_doc.py build/finc
```

A row is only added when its `prefix`/`contains` pair matches every call site in the
family and nothing outside it. That is enforced, not asserted:
`DiagnosticHelp.NoTwoCodesMatchTheSameMessage` and
`DiagnosticHelp.EveryRegisteredPrefixIsUniqueOrDisjoint` in
`tests/test_diagnostic_help.cpp` apply the real match rules from `diagnosticRules()`.

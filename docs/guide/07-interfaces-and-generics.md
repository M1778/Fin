# 7. Interfaces and generics

## Declaring an interface

An `interface` lists what an implementor must provide. Members are terminated with `;`,
methods have no body, and `pub` marks what is visible:

```fin
interface Printable {
    pub fun to_string() <string>;
}
```

An interface may require a *field* as well as a method:

```fin
interface Person {
    readonly name <string>,
}
```

and it may take generic parameters:

```fin
interface GetVal<T> {
    pub fun get_val() <T>;
}
```

An interface may declare operators, which is how `operators::std` describes every
overloadable operator:

```fin
interface Addable<T> {
    pub operator + (other: <T>) <T>;
}
```

Interfaces can use the section-label form too:

```fin
interface IStream {
  priv:
      stream_length <int>;
      pointer <int>;

  pub:
    fun seek(offset: int) <bool>;
    fun read(nbytes: int) <[char]>;
}
```

## Implementing at the declaration

A struct names the interfaces it satisfies with a colon and angle brackets — the same
syntax as naming a base type:

```fin
@define printf(fmt: string, ...) <noret>;

interface GetVal<T> {
    pub fun get_val() <T>;
}

struct Holder: <GetVal<int>> {
    val <int>,

    pub fun get_val(self: &Self) <int> {
        return self.val;
    }
}

fun main() <noret> {
    let h <Holder> = Holder { val: 7 };
    printf("%d\n", h.get_val());
}
```

Multiple interfaces are comma-separated: `struct Fin: <Person, Beautiful> { ... }`.

## Implementing in a separate block

`implements` attaches an implementation to an already-declared type:

```fin
struct MyStruct {
    val <int>
}

MyStruct implements <GetVal<int>> {
    pub fun get_val() <int> {
        return self.val;
    }
}
```

This is how the corpus adds an interface to a type it did not declare, and it works for
operators as well:

```fin
Point implements <Addable<Point>> {
    pub operator + (other: <Point>) <Point> {
        return Point { x: self.x + other.x, y: self.y + other.y };
    }
}
```

An `implements` block type-checks and registers its members, but it is not yet lowered to
machine code — a program containing one will not build with `-o`. Declaration-site
`struct X: <Iface>` is the form that runs today.

There is also a single-member form, `@implements Type::name = <expression>;`, which
overwrites a member the type declares or adds one it does not. It is what
`tests/samples/enums.fin` uses to attach an `unwrap` to an enum. Its `$type`-level
counterpart `@implements(struct, iface)` — asking at compile time whether a type satisfies
an interface — is declared in `docs/compiler-api.md` but has no implementation yet.

## Interfaces as bounds

The most common use of an interface is as a generic bound. `T: Printable` constrains a type
parameter:

```fin
@define printf(fmt: string, ...) <noret>;

interface Printable {
    pub fun to_string() <string>;
}

struct User {
    name <string>,

    pub fun to_string(self: &Self) <string> {
        return self.name;
    }
}

fun show<T: Printable>(item: T) <noret> {
    printf("%s\n", item.to_string());
}

fun main() <noret> {
    let u <User> = User { name: "Fin" };
    show::<User>(u);
}
```

Writing the interface directly as a parameter type is the shorter equivalent:
`fun show(item: Printable) <noret>`.

## Interfaces as runtime types

A struct converts to an interface it implements, and a value of interface type can hold
either implementor:

```fin
@define printf(fmt: string, ...) <noret>;

interface Person {
    readonly name <string>,
}

struct Fin: <Person> {
    readonly hate <Person>,
    name <string>,

    fun hate(someone: Person) <noret> {
        self.hate = someone;
        printf("Fin started hating %s\n", self.hate.name);
    }
}
```

An interface reference is two words at runtime — a pointer to the implementor's storage and
a pointer to a per-`(struct, interface)` vtable — and the vtable carries a byte offset for
every required *field* alongside a function pointer for every required method, because two
implementors need not agree on where a field sits.
`docs/adr/0027-an-interface-reference-carries-field-offsets-in-its-vtable.md` specifies the
layout. This lowers: `tests/samples/love.fin` is the corpus witness, and it builds with `-o`
and runs. The conversion is one-directional: an interface does not convert back to a struct.

## Generic parameters and bounds

Type parameters appear after the name of a function, struct, interface, enum or type alias:

```fin
fun identity<T>(a: T) <T> { return a; }
struct Box<T> { val <T> }
interface GetVal<T> { pub fun get_val() <T>; }
type ArrayType<T> = [T];
```

A bound follows a colon. It may be an interface, or a constraint set:

```fin
type Number = int | uint | float | short | long | ushort | ulong | double;

fun sort<T: Number>(array: &[T]) <noret> { ... }
```

Two bounds mean two different implementation strategies:

- An **unbounded** parameter, or one bounded by an ordinary interface, is
  *monomorphised* — the compiler emits one specialisation per concrete type.
- A parameter bounded by an **erasure marker** is *erased* — one implementation, with the
  type carried at runtime. `Castable` is the erasure marker the corpus uses. Erasure
  type-checks but is not yet lowered, so a `Castable` bound will not build with `-o`.

```fin
fun normal_generics<T>(a: T) <T> { return a; }                     // monomorphised
fun erased<T: Castable, U: Castable>(a: T, b: U) <int> {           // erased
    return cast<int>(a) + cast<int>(b);
}
```

A constraint set is only ever a bound, never a storage type — see
`docs/adr/0018-a-constraint-set-is-a-bound-never-a-storage-type.md`. `any implements <X>`
is the erased form that *does* have a representation, and it is how the standard library
spells "checked at runtime, carrying this bound":

```fin
pub type EnumType = any implements <Enum>;
```

## Explicit generic arguments

At a call site, `::<...>` supplies the arguments the compiler would otherwise infer:

```fin
show::<User>(u);
identity::<int>(7);
```

For a generic struct, the same `::<...>` goes between the type name and the brace:

```fin
let b <Box<int>> = Box::<int>{ val: 100 };
let c <&Collection<int>> = new Collection::<int>{};
```

Inference works from the arguments written at the call, and an annotation supplies what an
argument cannot say. Both directions are in use in the corpus.

## Meta-types

Four `$`-prefixed names are types whose values *are* types, for compile-time work:
`$type`, `$struct`, `$interface` and `$enum_member`.

```fin
fun compatible(iface: $interface, struct_: $struct) <bool> { ... }
pub fun keyidof(enum_member: $enum_member) <int>;
```

They resolve in every type position, and only these four — an unrecognised `$name` is an
undefined type rather than a silently accepted one. Nothing in the language produces a
`$type` *value* yet, so functions taking one are compiler intrinsics: declared without a
body, implemented in the compiler. Type literals (`struct { ... }` and `interface { ... }`
as expressions) parse and type-check as `$struct` and `$interface`, but instantiating one
needs the compiler API, which is not built.

Next: [enums and pattern data](08-enums-and-pattern-data.md).

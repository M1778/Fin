# 7. Interfaces and generics

## Declare an interface

An interface describes required fields, methods, or operators. Required methods
end in `;`. A struct can name the interface in `: <...>`.

```fin
interface Readable {
    pub fun read() <int>;
}

struct Reading : <Readable> {
    pub value <int>,
    pub fun read(self: &Self) <int> { return self.value; }
}

fun read_twice(item: Readable) <int> {
    return item.read() + item.read();
}

fun main() <noret> {
    let reading <Reading> = Reading{value: 6};
    blame read_twice(reading) == 12;
}
```

An interface reference contains a pointer to the implementor's storage and a
vtable pointer. It does not own the implementor. Keep that storage alive for every
use of the interface, including after a function returns. A reference escaping a
local can dangle because there is no lifetime checker.

Required fields have offset entries in the vtable. Reading fields through the
interface works; writing fields through an interface reference is currently
refused. Interface-to-interface conversions and generic interfaces used as runtime
value types also have backend limits.

## Attach an implementation

A separate `implements` block adds methods to an already declared struct. This
form now generates machine code for supported struct implementations.

```fin
interface Readable { pub fun read() <int>; }
struct Reading { pub value <int> }

Reading implements <Readable> {
    pub fun read(self: &Self) <int> { return self.value; }
}

fun main() <noret> {
    let reading <Reading> = Reading{value: 9};
    blame reading.read() == 9;
}
```

`@implements Type::member = expression;` is another member-attachment form.
Its syntax is distinct from a special-function call `@implements(type, interface)`.
Enum implementations have runtime limits because enum payloads themselves do.

## Bounds and specialization

A type parameter follows a declaration name; its bound follows a colon. Ordinary
generics specialize once per concrete type.

```fin
interface Readable { pub fun read() <int>; }
struct Reading : <Readable> {
    pub value <int>,
    pub fun read(self: &Self) <int> { return self.value; }
}

fun read_generic<T: Readable>(item: T) <int> {
    return item.read();
}

fun main() <noret> {
    let reading <Reading> = Reading{value: 8};
    blame read_generic::<Reading>(reading) == 8;
}
```

`item: T` with `T: Readable` and `item: Readable` are different runtime choices:
the former specializes for the concrete type; the latter dispatches through an
interface reference. Generic-bound checking has gaps at some instantiation sites;
write implementations that satisfy the declared contract even if an invalid call
currently passes the frontend.

Type parameters can appear on functions, structs, interfaces, enums, aliases, and
generic lambdas. For calls use `identity::<int>(value)`; for a type annotation use
`Box<int>`; for a struct expression use `Box::<int>{value: 1}`.

A constraint set is a bound, not a storage type:

```fin check
type Number = int | uint | float;
fun identity<T: Number>(value: T) <T> { return value; }
```

## Erasure and meta-types

An erasure marker such as `Castable` changes the generic strategy. The backend
cannot perform general boxing, casts, or calls through `any` yet. Some opaque
`any` storage can have a layout without supporting reads or writes; that is not
a dynamically typed application value. A template that nobody instantiates may
check and build while a call to it fails.

`$type`, `$struct`, `$interface`, and `$enum_member` describe compile-time values.
They are distinct types. Their presence in a signature does not imply that the
compiler can execute arbitrary reflection code. Component names and grants have
semantic checking; the broader execution design is in
[compiler-api.md](../compiler-api.md). See
[macros and compiler components](11-macros-and-preprocessor.md).

Next: [enums](08-enums-and-pattern-data.md).
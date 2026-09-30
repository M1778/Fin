# 6. Structs and classes

## Fields, defaults, and methods

Fields use angle-bracket annotations and commas. Construct a value with
`Type{field: value}`; omitted fields use declared defaults or zero initialization.

```fin
struct Point {
    pub x <int>,
    pub y <int> = 4,

    pub fun sum(self: &Self) <int> {
        return self.x + self.y;
    }
    pub fun move_x(self: &Self, amount: int) <noret> {
        self.x += amount;
    }
}

fun main() <noret> {
    let point <Point> = Point{x: 3};
    blame point.sum() == 7;
    point.move_x(2);
    blame point.x == 5;
}
```

`Self` means the enclosing type. `self: &Self` receives a pointer so mutation
reaches the original value. The compiler can inject `self` when omitted; writing
it explicitly makes the receiver clear. Call instance methods with `value.method()`.

A `static fun` has no receiver and uses `Type::method()`. A factory returning
`new Type{...}` returns `&Type`; a brace literal alone returns a value.

## Visibility

`pub` and `priv` can appear on a member or as section labels. `readonly` allows
initialization and writes by the declaring type, while rejecting external writes.
Use explicit visibility for library surfaces.

```fin fragment
struct Account {
priv:
    balance <int>,
pub:
    fun amount(self: &Self) <int> { return self.balance; }
}
```

## Generic structs

Type annotations use `Box<int>`; constructions use `Box::<int>{...}`.

```fin
struct Box<T> {
    pub value <T>,
    pub fun get(self: &Self) <T> { return self.value; }
}

fun main() <noret> {
    let box <Box<int>> = Box::<int>{value: 42};
    blame box.get() == 42;
}
```

Methods can declare their own type parameters. Keep their names distinct from
the enclosing struct's parameters. Unbounded concrete instantiations generate
specialized code; erasure-marked instantiations have different runtime limits.

## Constructors

A constructor initializes `self` and is invoked by `Type(arguments)`. Both the
`constructor` keyword and the enclosing type's name are accepted declarations.
Constructors **do run** in the current backend.

```fin
struct Counter {
    pub value <int>,
    constructor(initial: int) { self.value = initial; }
}

fun main() <noret> {
    let counter <Counter> = Counter(7);
    blame counter.value == 7;
}
```

Use one constructor per struct: constructor overload resolution is incomplete and
multiple constructors are refused. `Type{...}` uses field initialization rather
than invoking that constructor. Pass all constructor arguments explicitly even
when the frontend accepts a default.

## Destructors and ownership

`~Type()` defines cleanup. Normal scope exit invokes it for local values; `delete`
on a heap pointer invokes cleanup before freeing the allocation.

```fin
struct Tracked {
    pub id <int>,
    ~Tracked() { printf("drop %d\n", self.id); }
}

fun main() <noret> {
    {
        let value <Tracked> = Tracked{id: 1};
    }
    let heap <&Tracked> = new Tracked{id: 2};
    delete heap;
    printf("done\n");
}
```

```output
drop 1
drop 2
done
```

Fields clean up in reverse declaration order after the destructor body, followed
by bases. `return`, `break`, and `continue` also leave scopes; aborting with `blame`
does not unwind them. See [ADR 0030](../adr/0030-destructors-run-at-scope-exit.md).

Assignment copies a struct. Fin has no move or borrow checker to make copying
owning pointers safe. Avoid copying a struct whose destructor frees shared raw
storage; each copy can run the same cleanup. `~Self()` is not the spelling to use
in a generic type: use the declared type name.

## Operator methods

```fin
struct Number {
    pub value <int>,
    pub operator +(other: Number) <Number> {
        return Number{value: self.value + other.value};
    }
}

fun main() <noret> {
    let a <Number> = Number{value: 2};
    let b <Number> = Number{value: 5};
    let total <Number> = a + b;
    blame total.value == 7;
}
```

Declare `operator ==` when you need struct equality; the compiler does not
synthesize it. `operator []` and `operator []=` describe indexing. For library
containers, check whether their indexing path builds; named `get`/`set` methods
make the intended operation explicit.

## Classes and inheritance

A `class` is a value type like a struct. A base uses `: <Base>`; the same syntax
also lists interfaces, separated by commas.

```fin
struct Base { pub id <int> }
class Entry : <Base> { pub extra <int> }

fun main() <noret> {
    let entry <Entry> = Entry{id: 1, extra: 2};
    blame entry.id + entry.extra == 3;
}
```

`class` alone adds neither a vtable nor virtual dispatch. Interface references
provide runtime dispatch. Derived-pointer conversion and multiple-base method
layout have restrictions; test the exact conversion instead of assuming C++ rules.

Next: [interfaces and generics](07-interfaces-and-generics.md).
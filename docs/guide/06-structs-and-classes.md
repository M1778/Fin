# 6. Structs and classes

## Fields

A `struct` is a named group of fields. Fields are comma-separated and annotated in angle
brackets, and a field may carry a default value:

```fin
struct Point {
    x <int>,
    y <int> = 0
}
```

A struct value is built with the type name followed by braces naming the fields. A field
with a default may be omitted:

```fin
let p <Point> = Point { x: 3, y: 4 };
let q <Point> = Point { x: 1 };        // y defaults to 0
```

Fields are read and written with `.`:

```fin
let sum <int> = p.x + p.y;
p.x = 10;
```

## Methods

A method is a `fun` declared inside the struct body. Its first parameter is the receiver,
conventionally written `self: &Self`:

```fin
@define printf(fmt: string, ...) <noret>;

struct Point {
    x <int>,
    y <int> = 0,

    fun sum(self: &Self) <int> {
        return self.x + self.y;
    }
}

fun main() <noret> {
    let p <Point> = Point { x: 3, y: 4 };
    printf("%d\n", p.sum());
}
```

`Self` names the enclosing type, so `&Self` is a pointer to it. Writing the struct's own
name works equally well — `&Point` and `&Self` mean the same thing.

If a method omits the `self` parameter the compiler still injects the receiver, and `self`
is available in the body:

```fin
struct Counter {
    pub readonly count <int>,

    pub fun bump(new_val: int) <noret> {
        self.count = new_val;   // `self` is there even though it was not written
    }
}
```

Both spellings appear throughout the corpus. Writing `self: &Self` explicitly is clearer,
and it is what the standard library does.

Methods are called on values, not on types: `p.sum()`, never `Point.sum(p)`.

## Static methods

`static` declares a method with no receiver, called through the type with `::`:

```fin
struct Point {
    x <int>,
    y <int> = 0,

    pub static fun origin() <&Self> {
        return new Self{x: 0};
    }
}
```

`new` allocates on the heap, so a static factory that allocates returns a pointer. `new
Self{...}` and `new Point{...}` are both correct.

## Visibility

`pub`, `priv` and `readonly` control access. They can be written per member:

```fin
struct MyStruct {
    pub readonly v1 <int>,

    pub fun change_v1(new_val: int) <noret> {
        self.v1 = new_val;
    }
}
```

or as a section label that applies to everything after it:

```fin
struct Vec2<T> {
priv:
    x <T> = 0,
    y <T> = 0,

pub:
    fun length(self: &Self) <float> { ... }
}
```

The section form is what the standard library uses for anything with more than a couple of
members.

## Generic structs

Type parameters go in angle brackets after the struct name:

```fin
@define printf(fmt: string, ...) <noret>;

struct Box<T> {
    val <T>,

    fun get(self: &Self) <T> {
        return self.val;
    }
}

fun main() <noret> {
    let b <Box<int>> = Box::<int>{ val: 100 };
    printf("%d\n", b.get());
}
```

Note the two positions: `Box<int>` in a type annotation, `Box::<int>{...}` in a
construction. A method may take its own generic parameters, which are separate from the
struct's and may not reuse their names:

```fin
fun set_x<U>(new_x: U) <noret> {
    self.x = cast<T>(new_x);
}
```

## Operator overloading

`operator` declares an overload inside a struct body. The operator's symbol follows the
keyword:

```fin
@define printf(fmt: string, ...) <noret>;

struct Vector2 {
    x <int>,
    y <int> = 10,

    pub operator + (other: Vector2) <Vector2> {
        return Vector2{
            x: self.x + other.x,
            y: self.y + other.y
        };
    }
}

fun main() <noret> {
    let v1 <Vector2> = Vector2 { x: 1, y: 2 };
    let v2 <Vector2> = Vector2 { x: 3, y: 4 };
    let v3 <Vector2> = v1 + v2;
    printf("%d %d\n", v3.x, v3.y);
}
```

An operator's parameter may also be written bracketed — `operator + (other: <T>) <int>` —
and an operator can be generic. `operator []` and `operator []=` are the index forms; the
standard library declares both on its collection types, and a struct needs `operator []`
declared before `a[i]` type-checks on it at all.

One caveat: an index expression's *lowering* does not go through `operator []`, which is why
`lib/std/hashmap.fin` forwards to explicit `__get`/`__set` methods internally rather than
relying on its own operators.

## Constructors

A constructor is written as the struct's own name with a parameter list and a body:

```fin
struct Temp {
    degrees <int>,

    Temp(d: int) {
        return new Temp{degrees: d};
    }
}

fun main() <noret> {
    let t <auto> = Temp(20);
}
```

This type-checks. A constructor is not yet lowered to machine code, so a program that
declares one will not build with `-o` — brace initialisation (`Temp{degrees: 20}`) is what
runs today.

## Classes

`class` declares a struct that may name a base type. Everything else is identical —
including value semantics: a class is copied on assignment, exactly as a struct is.

```fin
@define printf(fmt: string, ...) <noret>;

class Named {
    pub:
      label <string>,

      fun show(self: &Self) <noret> {
          printf("%s\n", self.label);
      }
}

fun main() <noret> {
    let n <Named> = Named { label: "class value" };
    n.show();
}
```

Inheritance is spelled with a colon and the base in angle brackets. A struct can use the
same form:

```fin
struct Base {
    id <int>
}

struct Derived : <Base> {
    extra <int>
}

fun main() <noret> {
    let d <Derived> = Derived{id: 1, extra: 2};   // both fields, base first
}
```

The base's fields splice in at offset 0, so a derived value contains its base. Two
consequences of that are worth knowing:

- A `&Derived` is **not** accepted where a `&Base` is expected. The layout makes the
  conversion free, but no corpus sample exercises it, so the rule is left unruled and the
  analyzer refuses it.
- `class` buys no vtable and no dynamic dispatch. Nothing here makes a method virtual.

Destructors (`~Type()`) do not parse yet. `#[class]` is an attribute form that marks a
`struct` as a class, which is how `lib/std/error.fin` declares `Error`.

The reasoning behind class-as-value-type is in
`docs/adr/0026-a-class-is-a-struct-with-a-base-and-try-is-a-scope.md`.

Next: [interfaces and generics](07-interfaces-and-generics.md).

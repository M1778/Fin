# 8. Enums

## Plain enums

An `enum` names a set of members. A member may carry an explicit discriminant; the ones
that follow continue from it:

```fin
enum Status {
    OK = 0,
    ERROR
}
```

A member is referenced through its enum with `::`, and converted to an integer with `cast`:

```fin
@define printf(fmt: string, ...) <noret>;

enum Status {
    OK = 0,
    ERROR
}

fun main() <noret> {
    let a <Status> = Status::OK;
    printf("%d\n", cast<int>(a));
}
```

## Bringing members into scope

Writing `Status::` every time is noise. `extern ... as` gives a member a short name, and
`extern * from` lifts every member out at once:

```fin
@define printf(fmt: string, ...) <noret>;

enum Status {
    OK = 0,
    ERROR
}

extern * from Status;

fun main() <noret> {
    let a <Status> = Status::OK;   // still works
    let b <Status> = ERROR;        // and so does this
    printf("%d %d\n", cast<int>(a), cast<int>(b));
}
```

The single-member form is `extern Status::OK as OK;`. Both are covered again in chapter 10 —
they are general symbol operations, not enum-specific.

## Payloads

A member may carry values. The payload types are written in parentheses after the member
name:

```fin
enum Color {
    RGB(uint{8}, uint{8}, uint{8}),
    RGBA(uint{8}, uint{8}, uint{8}, uint{8})
}
```

A payloaded member is a *constructor*: name it with arguments and you get a value of the
enum.

```fin
fun main() <noret> {
    let c <Color> = Color::RGB(100, 200, 50);
    let d <Color> = Color::RGBA(0, 0, 0, 100);
}
```

Payload arguments are type-checked against the declared payload types.

## Positional members

`.N` reads slot `N` of whichever member the value holds:

```fin
enum Color {
    RGB(uint{8}, uint{8}, uint{8}),
    RGBA(uint{8}, uint{8}, uint{8}, uint{8})
}

fun main() <noret> {
    let c <Color> = Color::RGB(100, 200, 50);
    blame c.0 == 100;
}
```

Two things follow from "slot N of whichever member the value holds", and both matter:

- `.N` is *not* an index over the members. `Result`'s `Ok(T)` and `Err(E)` each carry one
  payload, so position `1` exists on neither and `c.1` is a diagnostic:
  `Enum 'Result' has no payload at position 1`.
- When the members disagree about what sits at position `N`, and which member the value
  holds is not statically known, the slot's type is `any`.

Reading a payload out of an enum whose members agree — as `Color` does, `uint{8}` at
position 0 either way — is fully checked.

## Generic enums

Type parameters go after the enum name, and members reference them in their payloads:

```fin
enum Result<T, U> {
    Ok(T),
    Err(U),
}

extern Result::Ok as Ok;
extern Result::Err as Err;

fun main() <noret> {
    let r <Result<int, string>> = Ok(10);
    r = Err("boom");
}
```

Note what fills in the type arguments there. An enumerator is a constructor whose generic
arguments come from what is written to it, so `Ok(10)` says `T` is `int`; `U` comes from the
annotation on the declaration. Where each member mentions only one parameter, one argument
plus the annotation is enough.

A member can also be declared with a field-style annotation, which is what
`lib/std/stdio.fin` does:

```fin
pub enum IOResult<T> {
    pub Err <IOError>,
    pub Ok <T>,
}
```

## Attaching methods

`@implements Type::name = <lambda>;` adds a member to an already-declared type, and an enum
can be the target. This is how the corpus gives `Result` an `unwrap`:

```fin
enum Result<T, U> {
    Ok(T),
    Err(U),
}

@implements Result<T, U>::unwrap = fun(enum_: Result<T, U>) <T> {
    return enum_.0;
}

fun main() <noret> {
    let r <Result<int, string>> = Result::Ok(10);
    r.unwrap();
}
```

The receiver of an enum method is its *first parameter* — `r.unwrap()` passes nothing
because `enum_` is the receiver. The block form, `Result<T, U> implements <IResult> { ... }`,
also parses and declares its target's generic parameters.

## Enum reflection

`enums::std` provides two intrinsics:

```fin
import { getkeyid, keyidof } from enums::std;

enum Status { OK, ERROR }

fun main() <noret> {
    let s <Status> = Status::OK;
    let k <int> = getkeyid(s);
}
```

`getkeyid(value)` takes a *value* of an enum and returns its key id. `keyidof(member)`
takes an enum *member* — a name, not a value — using the `$enum_member` meta-type. The
intended idiom for asking which member a value holds is
`getkeyid(enum_) == keyidof(Ok)`, and it is what the standard library drafts write:

```fin
import { getkeyid, keyidof } from enums::std;

enum Result<T, U> { Ok(T), Err(U) }
extern Result::Ok as Ok;

fun main() <noret> {
    let r <Result<int, string>> = Ok(1);
    let is_ok <bool> = getkeyid(r) == keyidof(Ok);
}
```

`keyidof` needs the member reachable as a bare name, which is what the `extern ... as` line
provides. The qualified spelling `keyidof(Result::Ok)` reports a type error — that path
resolves the member as a value or a constructor rather than as a `$enum_member`.

## What is not here

Fin has no `match` statement. It is on the roadmap and it will be keyword-introduced, for
the reason chapter 4 gives: a brace at statement start already means a block. Until it
arrives, comparison and `getkeyid` are how an enum is discriminated.

Payloads type-check but are not yet lowered to machine code, so an enum with a payloaded
member will not build with `-o`, reporting `codegen: a payload on enum member 'Color::RGB' is
not lowered yet`. A *generic* enum is refused the same way (`a generic enum 'Result' is not
lowered yet`) whether or not any member carries a payload. Plain, non-generic enums build and
run.

Next: [arrays and pointers](09-arrays-and-pointers.md).

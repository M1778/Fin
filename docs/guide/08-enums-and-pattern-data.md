# 8. Enums

## Plain enums run

An enum names members, optionally assigning integer discriminants. The next
member continues from the previous discriminant. Refer to a member with `::`.

```fin
enum State {
    Ready = 3,
    Busy,
    Done
}

fun main() <noret> {
    let state <State> = State::Busy;
    blame cast<int>(state) == 4;
    if (state == State::Busy) { printf("busy\n"); }
}
```

```output
busy
```

`extern State::Ready as Ready;` introduces an alias and `extern * from State;`
brings members into scope. Prefer qualified member names when clarity matters;
renamed-symbol lowering has limitations outside simple enum use.

## Payload syntax and its limit

Payload types follow a member name in parentheses. Generic parameters follow the
enum name. This example is deliberately **frontend-only**:

```fin build-error
enum Result<T, E> {
    Ok(T),
    Err(E)
}

fun main() <noret> {
    let result <Result<int, string>> = Result::Ok(42);
}
```

The backend refuses generic enums and enums with payloads. Their syntax is useful
for understanding library declarations, but a successful type-check does not
make `Result`, `IOResult`, or their methods runnable.

Payload member access uses `.0`, `.1`, and so on. The number selects a payload
slot of the current member, not a member of the enum. If members disagree about
that slot's type, the frontend may produce `any`; it does not establish safe
runtime narrowing.

## Representing a recoverable result today

For an executable, a plain enum plus concrete fields can represent a result.
Keep the status check beside every read of the corresponding value.

```fin
enum ParseState { Valid, Invalid }
struct CheckedNumber {
    pub state <ParseState>,
    pub value <int>
}

fun positive(number: int) <CheckedNumber> {
    if (number > 0) {
        return CheckedNumber{state: ParseState::Valid, value: number};
    }
    return CheckedNumber{state: ParseState::Invalid, value: 0};
}

fun main() <noret> {
    let result <CheckedNumber> = positive(7);
    blame result.state == ParseState::Valid;
    blame result.value == 7;
    let invalid <CheckedNumber> = positive(-1);
    blame invalid.state == ParseState::Invalid;
}
```

This pattern has no automatic tag/payload safety. The function and its callers
must preserve the relationship between status and fields.

## Reflection and matching

Fin has no `match` statement. Use `if` and plain-enum comparisons for executable
branches. `enums::std` declares `getkeyid(value)` and `keyidof(member)`; `keyidof`
expects the `$enum_member` meta-type. A bare alias to the member can type-check
where a qualified expression resolves as a constructor or value instead.

The library's enum reflection declarations and attached `Result` methods are
not evidence that payload dispatch executes. See [the library reference](12-standard-library-tour.md).

Next: [arrays and pointers](09-arrays-and-pointers.md).
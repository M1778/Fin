# `Ok(T)` in comparison position denotes the member

**Status:** Accepted

> 2026-10-21 status: representation is settled by ADR 0041. Payload construction,
> generic enum instantiation, and the normative `enums.fin` execution are covered
> by executable tests.

`enum_ == Ok(T)` asks whether the value holds the `Ok` member: the `T` is the
payload type as written in the member's declaration -- disambiguation, not a
value -- exactly as bare `Ok` already denotes the member where a
`$enum_member` is expected (`keyidof(Ok)`). A type is never a value; there is
no existential reading in which `T` stands for an arbitrary payload.

## Consequences

Lowering is a discriminant comparison over the tagged-union representation in
ADR 0041. `Ok(10)` constructs a value with that member's tag and payload; `Ok(T)`
in comparison position tests the member identity.

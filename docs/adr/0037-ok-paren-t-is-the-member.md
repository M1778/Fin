# `Ok(T)` in comparison position denotes the member

`enum_ == Ok(T)` asks whether the value holds the `Ok` member: the `T` is the
payload type as written in the member's declaration -- disambiguation, not a
value -- exactly as bare `Ok` already denotes the member where a
`$enum_member` is expected (`keyidof(Ok)`). A type is never a value; there is
no existential reading in which `T` stands for an arbitrary payload.

## Consequences

Lowering is a discriminant comparison, which waits on enum representation
(tag width and payload union are still unruled, and payloads refuse today).
`Ok(10)` construction in the same sample needs the same representation first.
Until then the diagnostic stands, and it stands for a missing representation
rather than a missing ruling.

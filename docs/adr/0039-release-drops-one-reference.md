# `release()` drops one reference

`rptr::release` decrements the shared count and frees the value at zero; it
nulls no handle, not even its own. A value with handles outstanding keeps
reading through them after a release, and `const.fin:96` -- which expected a
release to read null -- was repaired to assert the count instead.
Dispose-for-everyone would need a registry of handles, which no library has.

## Considered Options

- Dispose semantics (a release frees the value for all handles): unimplementable
  without a registry, and even a full release-to-zero ends in `delete`, not
  null -- the sample's assertion could not have held on any execution, at any
  refcount.
- Null-the-releaser (a release nulls the releasing handle's field): one
  handle reads null while its siblings read the live cell -- the handles
  disagreeing about one value, which is a worse answer than the count.

## Consequences

- Direct `.value` reads bypass `get()`'s released-guard. That encapsulation gap
  is parked, not solved here.
- The `wptr` paths, `get` and `is_alive` have still never executed; the
  `stdptr.fin` header says so where it lists what runs.

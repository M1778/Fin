# `release()` drops one reference

**Status:** Accepted

> 2026-10-21 status: the owner approved retained counter storage for weak handles and
> explicit weak-handle cleanup. The adopted amendment below closes the released-handle
> and weak-counter lifetime defects without changing the one-reference release rule.

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

## Adopted amendment (2026-10-21)

`rptr::release()` still drops exactly one strong reference and nulls no pointer
field. Each handle now records its released state before decrementing. Releasing
that same handle again, or using its counter/value methods afterward, blames
before touching shared storage. Outstanding strong handles continue to work.

`rptr::weak()` adds one weak reference. The final strong release destroys the
value, while weak references retain the strong, borrow, and weak counter cells.
`wptr::is_alive()` then answers false and `wptr::get()` blames. A weak handle is
cleaned up explicitly with `wptr::release()`; the final weak release frees the
counter cells when no strong references remain. A released weak handle keeps
answering false to `is_alive()` and refuses another `release()` or `get()`.

The readonly `restrict` snapshot remains a heap sibling and a strong reference
(ADR 0045): construction still starts with two strong references. No destructor
implicitly releases references. Each heap sibling obtained from `restrict`,
`alias()`, `readonly_view()`, `own()`, or `borrow()` requires one release and then
deletion through its owning local pointer; the original value handle only needs
release. Return an active borrow with `giveback()` before releasing it.

This does not introduce automatic copy tracking. A language-level copy aliases
the same storage without adding a reference and must not be released as an
independent handle. Raw pointer fields still bypass the method guards and may
dangle after the final strong release. Concurrent access remains caller-owned.

`tests/test_stdptr_lifetimes.cpp` pins both weak/strong release orders, repeated
release refusal, released-handle guards, unchanged raw pointer fields, and
LeakSanitizer-clean explicit cleanup of the constructor snapshot and heap siblings.

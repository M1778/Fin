# Prototypes are inference-first, insertion-ordered dictionary values

Accepted 2026-09-05 from the prototype design discussion.

## Decision

A prototype is Fin's structural dictionary value, written `{K, V}` in a type
position and `{ key: value, ... }` as a literal. The literal infers its key and
value types; an empty literal requires an expected type or explicit type
arguments. This syntax remains distinct from a nominal `HashMap<K, V>` while
being usable as its convenient, compiler-supported dictionary surface.

The first runtime implementation is a normal generic standard-library type
with compiler-recognized intrinsics. The compiler owns the intrinsic contract,
not the storage layout: the standard library may replace the implementation,
and users may build compatible custom map types through the relevant traits and
interfaces. The built-in prototype is therefore a powerful language feature,
without making every custom map a compiler special case.

The initial API is:

* `p[key]` reads and `p[key] = value` writes;
* `p.get(key)` reads and issues a runtime `Fin blames ...` failure when absent;
* `p.try_get(key)` is the non-throwing lookup and returns an option-like value;
* `p.contains(key)` tests presence and `p.remove(key)` removes an entry;
* `p.entries()` yields `Entry<K, V>` values with `.key` and `.value`.

Iteration preserves insertion order. `keys()` and `values()` may be wrappers
around `entries()` after the first implementation. An absent key is never
represented by a sentinel value: a generic `V` has no universally valid sentinel.

A prototype's keys use explicitly derived structural hash and equality. The
compiler derives them recursively for bools, integers, floats, strings,
pointers, arrays, and structs whose fields are themselves derivable. Unsupported
fields and recursive derivation cycles are compile-time errors. Float behavior,
including NaN and signed zero, must be specified by the hashing/equality trait
before float keys are enabled; it must not be inherited accidentally from raw
memory. Raw object bytes are never the default hash because padding, pointer
identity, and representation changes would make value semantics unstable.

## Consequences

This design gives Python-Dict-like ergonomics while retaining Fin's static
key/value types, generic safety, and predictable iteration. It also leaves room
for a specialized ordered hash table behind the stdlib intrinsic boundary.
The first implementation should prioritize a correct baseline and a replaceable
runtime abstraction over prematurely committing to open addressing, allocator
hooks, or iterator invalidation details.

The compiler work is staged: establish the prototype intrinsic interface and
sound tests, then lower the baseline representation, then optimize it. The
syntax and type rules must not depend on whether the backing table uses open
addressing, chaining, or another implementation.

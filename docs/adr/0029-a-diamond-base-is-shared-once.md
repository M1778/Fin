# A diamond base is shared once; any other second base is still refused

`deeptest2.fin:83` writes `MultiInherit: <Person, Student>` where `Student: <Person>`, so `name` arrives twice. We share the ancestor: `MultiInherit` lays out as `Student` plus its own fields, with `Person`'s bytes once at offset 0, and `super::<Person>::name` and `super::<Student>::name` name the same slot. Both upcasts stay free.

## Considered Options

- Duplicate copies with adjusted upcasts: a second-base ABI nobody has ruled, and the attempted backend-only deduplication segfaulted and was reverted.
- Refuse all multi-base including diamonds: keeps the current `Layout.cpp` refusal but leaves the sample's own diamond shape without an answer.

## Consequences

Only transitive sharing is allowed. Two unrelated bases still refuse with the existing diagnostic, and a duplicate field name with no common ancestor still refuses rather than being chosen by order. `baseSharesLayout` needs no change: shared bytes at offset 0 already pass its offset comparison, so `Person` methods called through `MultiInherit` keep working.

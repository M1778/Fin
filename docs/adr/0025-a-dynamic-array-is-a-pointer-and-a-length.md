# A dynamic `[T]` is a pointer and a length, and nothing else

`[T]` — an array with no extent in its type — is represented as two words:

```
{ ptr, len }        ptr : T*     the elements, heap-allocated
                    len : int    how many there are
```

It **carries its length**, it **owns heap memory**, and it has **no capacity field**. A `&[T]` is
a pointer to that pair, not a pointer to the elements.

This is distinct from `[T, N]`, which has its extent in its type, needs no length word, and is
already lowered. The two are not one feature with a number missing.

## Why the corpus decides this, and not taste

The owner was asked to choose between a fat pointer and a growable vector and **declined**,
ruling that ADR 0008 applies. It does, and it answers — so this ADR is a derivation, not a
preference. Every citation below was re-taken from the files on 2026-08-28 rather than carried
from a previous summary.

### It carries its length

`.length` is read off a `[T]` at `tests/samples/arrays.fin:12`, `:17`, `:18` and
`tests/samples/stdlib/stdio.fin:114`, `:130`, `:135`, `:160`.

The decisive one is `arrays.fin:12`. Line 11 declares `fun sort<T: Number>(array: &[T])`, and
line 12 reads `array.length` — **through the `&[T]`**, inside a callee that received it. The
array's own note says so in as many words: *"`array.length` at 12:9 resolves, including through
the `&[T]` it is written on."* A callee handed a bare pointer cannot answer that question. The
length must travel with the value.

`lib/std/stdio.fin`'s draft states the same conclusion from the other side: *"a `new [char, n]`
is a `[char]`, not a pointer to a fixed-size one … `_temp.length` reads a length off the
allocation, **which a pointer does not have**."*

The length is an `int`, pinned from both directions:
`Soundness_BuiltinMembers.ADynamicArrayHasALengthOfTypeInt` requires `let n <int> = a.length`
to compile, and `Soundness_HeapArrays.ItHasALength` requires
`let s <string> = a.length` to report `expected 'string', got 'int'`.

### It owns heap memory

Every `[T]` in the corpus is born from `new [T, n]` with a **runtime** extent and freed by
`delete`:

| site | extent |
| --- | --- |
| `lib/std/stdio.fin:138` | `new [char, want - self.pointer]{}` — computed |
| `lib/std/stdio.fin:150` | `new [char, nbytes + self.stream_length]{}` — computed |
| `tests/samples/stdlib/collection.fin:54` | `new [T, amount]{}` — a parameter |
| `lib/std/collection.fin:112` | `new [T, 1]{}` |

`collection.fin:46` frees what `:54` allocated — one buffer at both ends. **So a `[T]` needs an
allocator.** An earlier note claimed a fat pointer needs none; that was wrong, and the corpus
corrected it — the elements have to come from somewhere, and `new` is where.

### No capacity field

Nothing in the corpus grows a `[T]` in place. Every growth site is a `Collection<T>` method, and
`Collection` *wraps* a `[T]` rather than being one:

* `tests/samples/stdlib/hashmap.fin:16-17` declare `priv keys <Collection<T>>` and
  `priv values <Collection<U>>`, so the `self.keys.push(key)` on `:37` is a `Collection` method.
* `lib/std/collection.fin:51` declares the wrapped array as `_arr <[T]>`.
* `lib/std/collection.fin:77` is how it grows: `let grown <[T]> = new [T, amount]{};` — a
  **fresh buffer**, copied into. Its own comment at `:21` says *"`allocate` grows into a fresh
  buffer here instead."*

Growth is built **on** `[T]`, not **into** it. A capacity word would be a field every `[T]` in
the program pays for so that one library type need not reallocate — and that library reallocates
anyway.

### The other shapes it has to support

Passed by value (`const.fin:61`) and by reference (`arrays.fin:11`), returned
(`stdio.fin:87`, `prototypes.fin:10`), stored in fields (`collection.fin:51`, `stdio.fin:83`,
`:98`, `deeptest2.fin:111`), indexed and assigned through (`collection.fin:59`
`self._arr[self.filled] = item;`, `:69` `return self._arr[index];`), and used as a generic
argument (`const.fin:98` `rptr<[int]>`). Two words in a struct satisfies all of them; a
header-word-before-the-elements scheme satisfies them too but pays an indirection on every
`.length` and cannot represent a `[T]` that does not own its buffer.

## What this does not decide

* **Bounds checking.** Nothing here says an index is checked against `len`. The corpus writes no
  site that would tell a checked lowering from an unchecked one.
* **Whether two `[T]`s can share a buffer**, and what `delete` does about it. `collection.fin`
  allocates and frees the same buffer in one type, which is the only ownership pattern witnessed.
* **`[T]` in a foreign signature.** A two-word aggregate's ABI is a platform question, and no
  corpus site passes a `[T]` across `extern`.
* **Growing a `[T]` in place.** Deliberately unrepresentable, per the section above. If a corpus
  site ever asks for it, that is a new ruling and not an extension of this one.

## Consequences

Two `Soundness_` tests currently assert this question is **undecided**, and both must be
inverted — with their arguments preserved, per the convention — by whoever implements this:

* `Soundness_Layout.ADynamicArrayStillHasNoLayout` (`tests/test_layout.cpp:738`) requires
  `LayoutEngine` to refuse a `[T]` with the word "undecided" in the refusal. Under this ADR a
  `[T]` has a layout: two words, `align` of a pointer.
* `Soundness_Codegen.ADynamicArrayIsRefused` (`tests/test_codegen.cpp:1161`), and the section
  comment above it at `:927` that names the fork and defers to `Layout.cpp` at the same one.

Neither is relaxed. Each becomes the positive statement of what is now true, keeping the original
reasoning inside it, because the reasoning was correct while the question was open and the record
of *why* it was open is what stops it being reopened by accident.

`arrays.fin`, `arrays_enums.fin` and `deeptest1.fin` list a `[T]` variable as their **first**
codegen refusal. That is not a promise of three samples: `finc -c` stops at the first refusal, so
what sits behind each becomes visible only once this lands. Measure after, not before — the same
mistake has been made twice on this corpus already.

**ADR 0024 is deliberately skipped**, not free: `docs/HANDOFF.md` §8 reserves it for the wave-4
`$type` ruling, design-and-plan-only, which was researched and never written up. Taking 0024 for
something else would strand that.

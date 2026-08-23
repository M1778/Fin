# The conditional expression is written `cond : then ? otherwise`

Fin's conditional expression puts both arms after the condition with `:` first and `?` second, so
`result == -1 : false ? true` yields `false` when the condition holds. The grammar shipped the C
order (`cond ? then : otherwise`) and no Fin code ever used it; the two sites in the corpus that
write a conditional both use this order, and the one C-order occurrence is inside a `#cdef`, which
is C preprocessor text rather than Fin.

The order is not merely a taste inherited from the samples. `?` is already Fin's postfix
denullifier, so in C order the parser cannot tell `x ? -1 : 2` from `x?` followed by a binary
minus without unbounded lookahead — and the same holds for a leading `*`, `&`, or `(`. Leading with
`:` has no such collision, because `:` never follows a complete expression except inside a struct
literal or a macro argument list, where the grammar already distinguishes those positions.

## Consequences

The word `?` reads as "otherwise" in a conditional and as "unwrap or fail" in a denullify. Those
are unrelated meanings on one symbol, and this decision buys parseability with that cost rather
than removing it.

Readers arriving from C will transpose the arms, and transposed arms compile silently and return
the wrong value. There is no diagnostic that can catch this, so it is the one place in the language
where familiarity actively misleads.

The C-order productions at `parser.y:1174` and `:1300` are deleted rather than kept as an
alternative spelling. Accepting both would make `cond : a ? b` and `cond ? a : b` mean opposite
things by punctuation order alone.

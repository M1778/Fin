# `m1778` means "not implemented" and nothing else

`m1778` appeared in the corpus in two unrelated roles: as an expression meaning "not
implemented" (`blame m1778;`) and as a statement wrapper that turned its body into an infinite
loop with an implicit replacement (`m1778 { break; }` in `loops.fin`, `m1778 kilo = 19;` in
`variables.fin`). We kept only the first. One token doing two jobs that share no intuition
costs every reader the same double-take, and the loop form had no consumer, no grammar
production, and no stated purpose; the not-implemented form is idiomatic across the stdlib.
The loop-form samples were rewritten rather than the language extended to admit them.

## Consequences

`blame m1778;` must be given a grammar production — `%token KW_M1778` exists with zero
productions, so the canonical idiom is a syntax error today.

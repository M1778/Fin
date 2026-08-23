# A bare brace in statement position opens a scope

`{` at the start of a statement always opens a block. A brace-initialised value only ever appears
after something that introduces it — `st{}`, `= map!{...}`, `= {...}` — so the two never compete for
the same position.

`variables.fin:15-18` opens a scope with a bare block, commented "Custom Scope", and fails. Not
because of the `let` inside it: the `{` is being parsed as an expression, so any statement in the body
is rejected. The reproducer is six lines and the error points at the `let`, three lines away from the
cause, which is what makes this worth recording rather than just fixing.

The alternative was to disambiguate by lookahead — scan past the brace and decide from what follows
whether this is a block or a value. That was rejected as unbounded: `{ a` could open a scope
containing an expression statement or begin a value with field `a`, and no fixed amount of lookahead
separates them in general. Fin already pays for one unbounded-lookahead problem in the ternary, which
is why `?` was moved to the `otherwise` position (ADR 0005); acquiring a second one for a construct
that appears once in the corpus is a bad trade.

## Consequences

A brace-initialised value can never be a whole expression statement. `{ x: 1 };` on its own is now a
scope containing a labelled statement or a syntax error, not a discarded value — which costs nothing,
since a statement that constructs a value and throws it away had no purpose.

The rule is positional, so the same characters mean different things in two places, and the
difference is invisible on the line itself. `f({ a: 1 })` is a value and `{ a: 1 }` at statement
start is not.

This constrains any future statement form that would want to begin with a brace. Pattern-matching
arms, block expressions that yield a value, and struct-literal statements all now have to be
introduced by a keyword rather than by a brace. `match` is the one already on the roadmap, and it is
keyword-introduced, so nothing pending is blocked.

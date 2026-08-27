# AST nodes carry a kind discriminator and traversal has a default

The AST had one dispatch mechanism: a 55-method pure-virtual `Visitor` that every consumer
implements in full. We are adding two things alongside it — a `NodeKind` discriminator on
`ASTNode`, and a `StructuralWalk` base class that visits children by default so a consumer
overrides only the nodes it cares about.

The flat visitor was already failing before either codegen or compiler components existed:
`Attribute` and `GenericParam` have no `visit` overload at all, so their `accept` bodies are
empty and cloning a generic declaration silently replaces its attributes and generic parameters
with nulls; `ASTPrinter` is not a `Visitor` and reports `Unknown Node` for five node types; and
dispatch from outside a visitor is a `dynamic_cast` chain. Compiler components make this
decisive rather than merely untidy, because a component that rewrites code at a scope-exit event
is dispatching over arbitrary nodes from outside any visitor — the case the existing design
serves worst.

## Consequences

Two mechanisms now exist for the same job. `Visitor` remains correct for consumers that must
handle every node exhaustively and want the compiler to say so; `StructuralWalk` is for
consumers that care about a handful. Choosing wrongly costs a silently-skipped subtree, which is
the failure mode `Visitor` was protecting against.

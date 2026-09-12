# `#[export]` is import-visibility; emission stays shared

`#[export]` on a declaration means the module system hands it to importers.
It does not change what the backend emits: every symbol this compiler
publishes -- methods, constructors, operators, generic instances -- keeps the
`linkonce_odr` linkage that lets identical definitions in two objects merge
into one (AGenericMethodIsOneSymbolAcrossTwoObjects and its kin pin that
bargain). Giving an exported instantiation external linkage instead would make
a correct two-object link fail with duplicate symbols, because emission is
per use, not placed once per program.

## Consequences

The backend accepts flag-form `#[export]` where a template or generic
function is checked and otherwise ignores it. A valued form still refuses: it
names nothing this file reads. If emission ever moves to single placement
(one defining object, `extern` elsewhere), this decision is the one to
revisit -- external linkage belongs to that design, not this one.

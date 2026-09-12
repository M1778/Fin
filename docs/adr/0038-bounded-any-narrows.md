# `Any<I>` narrows to implementors of `I`

A bound written `Any<Printable>` -- and a bare `any implements <I>` in the
same spirit -- means anything implementing the interface: method calls
through such a value resolve against the bound. This is what makes
`object.format_str()` well-typed for `object: X` under `X: Any<Printable>`.

## Considered Options

- Keep refusing: leaves every bounded generic unusable past its declaration,
  including the standard library's own `print`/`println`.
- Treat the bound as plain `any`: what the backend does today by discarding
  the arguments, and exactly the silence the alias gap is booked for.

## Consequences

Two mechanisms have to exist first: generic aliases (a parameter list to bind
the arguments to -- `type Any = any` takes none today), and bound-checked
method resolution through the bound's interface. Until both land, `Type 'X'
does not have methods` stands, and it stands for missing machinery rather
than a missing ruling.

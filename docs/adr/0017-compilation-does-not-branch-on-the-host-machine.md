# Compile-time code may read the host machine but may not branch on what it reads

A compile-time value obtained from the machine running the compiler — `compiler.system.get_available_memory`,
`get_total_memory`, `get_memorycard_model`, and anything added beside them — is **tainted**. Using a tainted
value as a value is allowed. Branching on one at compile time is a hard error, and the taint propagates
through `@special` calls, so a `@special` that returns a host value taints the binding its caller puts the
result in.

The property being defended is that the same source compiles to the same program. Give compile-time `if`
the ability to read the build machine's free memory and two developers compile the same file into two
different programs, with nothing in the source to say why. Worse, the failure mode is a build that passes on
a laptop and fails in CI, or the reverse, for a reason no amount of reading the code will reveal.

## The corpus cost is zero, but not for the obvious reason

It would be easy to record this as "nothing in the standard library branches on a host value", and that would
be wrong. `memory.fin:12` is exactly such a branch:

```fin
fun? falloc(size: size_t) <&void> {
  if (size > @GET_MEMORY_LIMIT()) {

  }
  return @Alloc(size);
}
```

It costs nothing because `falloc` is a plain `fun`, not a `@special`. The comparison runs at run time against
a constant the compiler folded in, and this rule governs compile-time control flow only. Stating the reason
precisely matters, because the next person to open `memory.fin` will find that `if` and conclude the rule was
violated on the day it was written.

## A defect that line does expose, flagged rather than ruled

`GET_MEMORY_LIMIT()` (`memory.fin:38-40`) is a `@special` returning the **build** machine's available memory
into ordinary code. So the compiled binary carries the build machine's free RAM as its allocation ceiling: a
program built on a large CI runner would permit allocations a small target cannot serve, and one built on a
small machine would refuse allocations the target could have served. The `if` body at `:13-14` is empty and
the function is `fun?`, so the intent — return nothing when the request exceeds the limit — was sketched and
never finished.

That is a defect in the sample, not in this rule, and repairing it needs a decision this ADR does not make:
whether `falloc` consults the host at run time, and through what. Recorded here because the taint rule is
what makes the defect visible, and because a reader who accepts the rule will otherwise assume the sample
was checked against it and passed.

## Why taint rather than forbidding the reads

`mem_info()` (`memory.fin:27-33`) reads three host values, formats them into a prototype, and returns it for
a program to print. Nothing there threatens reproducibility, because the values flow out as data rather than
in as decisions. Forbidding the reads would delete the only host introspection the corpus has, to fix a
problem it does not have. Tainting them costs that use nothing and stops the one thing that breaks builds.

C's `__DATE__` and `__TIME__` are this hazard in miniature, and they are the reason reproducible-build
toolchains carry a special case for them (`SOURCE_DATE_EPOCH`). Nix and Bazel go further and remove the host
from the build environment altogether, because retrofitting reproducibility onto a toolchain that leaked it
is far harder than not leaking it.

## Consequences

Taint is a property the interpreter's value model must carry, alongside the Fin-value/compiler-object span
ADR 0006 already requires of it.

The check lives at branch points and nowhere else, which is what keeps it cheap — no dataflow analysis, just
a flag riding along on values.

The diagnostic has to name the host operation the taint came from, not merely the branch that used it. A
branch is usually several calls away from the read, and "cannot branch on a tainted value" without the origin
leaves the user to find it by hand.

A future component that reads the host in some other way — the clock, the environment, the filesystem — joins
this rule rather than getting its own. The rule is about the host, not about memory.

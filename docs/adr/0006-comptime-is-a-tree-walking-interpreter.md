# Compile-time execution is a tree-walking interpreter inside the compiler

A `@special` function runs during semantic analysis, executed by an interpreter that walks the AST
on the host. `$type` is a real compiler-side object with structure the interpreter can inspect, and
a compiler component is a native C++ function bound into the interpreter's environment. The
alternatives were to lower `@special` bodies and run them through an LLVM JIT, or to restrict them
to an expression subset the analyzer constant-folds.

The folding option was ruled out by ADR 0003: a memory-management library has to walk a scope's
live variables and emit calls, which is not a fold, so folding would have made the component API
too weak to keep the language's central promise. The JIT option was ruled out by ordering. A JIT
needs the backend to lower code before analysis has finished, and ADR 0002 says the backend is
being written fresh — so the JIT route makes the component API wait on the backend, when the
component API is the thing the standard library is already written against.

`pyprototype` offers nothing to inherit here. Its `@special` handler compiles arguments to runtime
LLVM values and emits the body into the caller's builder, so `@special` was an inliner, not
compile-time execution at all; only `@hasattr`, `@unsafe_unbox`, and `@name` were genuinely folded,
and `$type` did not exist.

## Consequences

The compiler contains two execution models for one language. Fin semantics must agree between the
interpreter and the backend, and where they disagree the bug is invisible — code behaves one way at
compile time and another at run time.

Everything reachable from a `@special` body must be interpretable. The standard library therefore
acquires a line through it: code a `@special` may call, and code it may not. That line is a language
constraint, not an implementation detail, and it has to be visible to the person writing the
library.

The interpreter needs a value model that spans Fin values and compiler-side objects, because a
`@special` body manipulates both in the same expression.

## The line has been widened, and this corrects a measurement rather than conceding a principle

The line recorded here was measured off the interpreter's reachable closure in the standard library as
it stands, and the corpus demands more than that closure does. `literal_interface.fin:4` is
`if (@implements(struct_, iface) == true)`; `:17` is `if (option == IFaceOptions::First)` returning an
anonymous `interface { ... }` literal from either arm; `literal_struct.fin:27` is
`if (!@defined("printf"))` guarding an `@define`. So `if`/`else`, unary `!`, and calls into `@special`s
are required by the specification, not by anyone's convenience.

Admitted: `if`/`else`, unary `!`, comparison, calls to `@special` functions, and quote-and-splice.
Refused: recursion, and every loop form.

The no-hang guarantee survives this intact, which is what makes the widening affordable. Compilation can
only fail to terminate through iteration or recursion, and `if`, `!`, comparison and non-recursive calls
are all bounded by the size of the program text. D's specification concedes that "if the function goes
into an infinite loop, it may cause the compiler to hang"; Zig buys the same ground back afterwards with
`@setEvalBranchQuota`. Fin needs neither a fuel counter nor a diagnostic for exhausting one.

What it does need is a **call-graph cycle check over `@special` functions**, because recursion is now the
only remaining route to non-termination and nothing else refuses it. That check is the entire price of
this amendment, it is local to the wave that builds the interpreter, and it is the one thing standing
between this line and D's hang.

One naming hazard belongs with this. `@define` injects a declaration and `@defined` tests whether a name
is declared — one letter apart, opposite meanings. The corpus chose those names so they stand, but
confusing them has to produce a diagnostic that names both.


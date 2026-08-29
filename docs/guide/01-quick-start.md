# 1. Quick start

This guide teaches Fin as the compiler in this repository actually implements it. Every
example in it was compiled with `finc` before it was written down, and where a construct
type-checks but does not yet produce machine code, the chapter says so.

## Building the compiler

`finc` is built by the script at the repository root:

```
./build.sh
```

It needs `cmake`, `conan`, `bison` and `flex` on `PATH`, plus LLVM at the major version
pinned in `CMakeLists.txt`. The script reads that version out of `CMakeLists.txt` rather
than keeping its own copy. Useful options:

```
./build.sh --release      # Release instead of Debug
./build.sh --clean        # discard the build directory first
./build.sh --no-test      # skip the test suite
```

When it finishes it prints the path to the compiler, which is `build/finc`.

Check that the binary works and identify it:

```
$ finc --version
finc 0.4.0 (contract 1)
```

The first number is the release. The second is the *machine contract* version — the number
that tooling wrapping `finc` should branch on. `docs/finc-interface-contract.md` is the
full specification of that surface.

## Your first program

Put this in `hello.fin`:

```fin
@define printf(fmt: string, ...) <noret>;

fun main() <noret> {
    printf("Hello, world!\n");
}
```

Two things are happening.

`@define` declares a function that exists outside Fin — here, C's `printf`. The
declaration gives its name, its parameters and its return type, and no body. `...` marks
it variadic. Fourteen files in the sample corpus open with this exact line, so it is the
idiomatic way to reach C's output for now.

`fun main() <noret>` declares the entry point. The return type goes in angle brackets
after the parameter list, and `noret` means the function returns nothing.

## Compiling and running

With no output flag, `finc` only checks the program:

```
$ finc hello.fin
Build Successful.
$ echo $?
0
```

Exit `0` means zero diagnostics. Add `-o` to produce an executable:

```
$ finc hello.fin -o hello
Build Successful.
$ ./hello
Hello, world!
```

A note on `printf` specifically: it is *ambient*, so a program may call it with no
declaration and no import at all. This checks and builds too:

```fin
fun main() <noret> {
    printf("Hello, world!\n");
}
```

`printf` is the one name in the standard library marked `#[global]`
(`lib/std/stdio.fin`), which is what makes it visible everywhere with no import. Writing
the `@define` line yourself is still fine and still idiomatic — the two declarations are
identical, so the compiler treats them as one fact rather than a conflict. Every other
name in the standard library must be imported; see chapter 10.

## Exit codes

`finc` communicates entirely through its exit code and stderr:

| Code | Meaning |
| --- | --- |
| `0` | compiled with zero diagnostics |
| `1` | the source was rejected |
| `2` | bad command line, or the input could not be read |
| `3` | the compiler itself failed |

stdout is reserved: only `--help` and `--version` are ever written there. Diagnostics go
to stderr, which means you can pipe a program's own output without compiler chatter mixing
into it.

## The flags you need first

**`-o <path>`** — build an executable at `<path>`. Without it, `finc` type-checks and
stops. Building requires a top-level `fun main` with a body.

**`-c`** — compile to an object file and do not link. Combine with `-o` to name it:

```
$ finc hello.fin -c -o hello.o
```

**`-I <path>`, `--include <path>`** — add a module search path. Repeatable. This is how
`import` finds a module that is not beside the importing file:

```
$ finc main.fin -I ./src -I ./vendor
```

**`--fin-libs <paths>`** — library search paths, separated by the platform's path
separator (`:` on Linux). This *replaces* the `FIN_LIBS` environment variable rather than
adding to it, and it also suppresses the standard library that ships beside the compiler.
That is deliberate: a build that pins its library paths gets exactly those paths, so
nothing can appear underneath the pin. Leave it off and you get the bundled `lib/std`,
which is what the standard-library chapter assumes.

## The rest of the surface

```
-O0, -O1, -O2, -O3     optimisation level for a -o build (default -O0)
--diagnostics=<fmt>    human (default) or json
--color=<when>         auto (default), always or never
--debug-ast            print the parsed AST
--debug-sema           print semantic analysis details
--debug-codegen        print what the backend lowers and the link command
--no-check             skip semantic analysis (unsafe)
```

`--diagnostics=json` emits one JSON object per line on stderr, ending with a summary
object. It exists for tooling; `docs/finc-interface-contract.md` documents the schema and
its compatibility rules.

## Reading a diagnostic

Make a deliberate mistake — annotate an integer as a string:

```fin
fun main() <noret> {
    let x <string> = 10;
}
```

```
error: Type mismatch: expected 'string', got 'int'
   --> bad.fin:2:22
   |
 2 |     let x <string> = 10;
   |                      ^^ here
```

The caret span points at the expression whose type was wrong, not at the declaration. Fin
diagnostics aim at the thing that has to change.

Next: [variables and types](02-variables-and-types.md).

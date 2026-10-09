# Fin

Fin is a systems programming language with an LLVM backend. This repository
holds its reference compiler, `finc` (C++ sources under `src/`), a self-hosting
compiler written in Fin itself (under `finc/`), and the standard library (under
`lib/std`). The package manager, `finn`, is a separate project.

**Writing a project in Fin with an AI agent? Give it
[the agent guide](docs/agent-guide.md) first.** It covers syntax, working
patterns, compiler limits, and the commands that distinguish checking code from
running it.

One honest caveat before the tour: the frontend accepts some constructs the
backend cannot generate yet, so a file that passes `finc hello.fin` may still
fail `finc hello.fin -o hello`. Always build and run before treating code as
done.

## Install

Fastest path (clones into `~/Fin`, or `$FIN_DIR` when set, then builds and
smoke-tests a backend compiler):

```sh
curl -fsSL https://raw.githubusercontent.com/M1778/Fin/master/install.sh | sh
```

Details, failure modes, and what the script checks first live in
[INSTALL.md](INSTALL.md). To build this checkout by hand on Linux or macOS,
install a C++20 compiler, CMake 3.20 or newer, Conan 2, Bison 3.2 or newer,
Flex, Python 3, the LLVM development headers for the major pinned in
`CMakeLists.txt` (currently 22), and a C driver such as `cc` for linking —
then run `./build.sh --release` from the repository root. Platform specifics
are in the [quick start](docs/guide/01-quick-start.md).

## Five-minute tour

Save this complete file as `hello.fin`:

```fin
fun main() <noret> {
    printf("Hello, Fin!\n");
}
```

```output
Hello, Fin!
```

`fun` declares a function; its return type follows the parameters in `<...>`,
and `noret` means no value. `printf` is ambient — no import needed. Two
commands, two different promises:

```sh
finc hello.fin             # syntax and type checking only
finc hello.fin -o hello    # object generation and linking
./hello                   # run the program
```

Structs are values with methods. `Self` is the enclosing type, and
`self: &Self` receives a pointer so mutation reaches the original:

```fin
struct Point {
    pub x <int>,
    pub y <int> = 4,

    pub fun sum(self: &Self) <int> {
        return self.x + self.y;
    }
    pub fun move_x(self: &Self, amount: int) <noret> {
        self.x += amount;
    }
}

fun main() <noret> {
    let point <Point> = Point{x: 3};
    blame point.sum() == 7;
    point.move_x(2);
    blame point.x == 5;
    printf("point = (%d, %d)\n", point.x, point.y);
}
```

```output
point = (5, 4)
```

Memory is explicit. A `[T]` is an array handle — a pointer plus a length —
allocated with `new` and released once with `delete`:

```fin
fun main() <noret> {
    let count <int> = 4;
    let buffer <[int]> = new [int, count]{};
    blame buffer.length == 4;
    buffer[3] = 12;
    blame buffer[3] == 12;
    printf("buffer[3] = %d\n", buffer[3]);
    delete buffer;
}
```

```output
buffer[3] = 12
```

Copying a handle does not copy its elements, so keep one owner and free each
allocation once. There is no borrow checker.

Errors use `blame`. With a boolean it asserts; failure prints a location and
message to stderr and aborts — it does not unwind or run cleanup, and `catch`
cannot recover from it. Use explicit status values for recoverable errors:

```fin
fun divide(numerator: int, denominator: int) <int> {
    blame denominator != 0, "denominator must be nonzero";
    return numerator / denominator;
}

fun main() <noret> {
    blame divide(12, 3) == 4;
    printf("12 / 3 = %d\n", divide(12, 3));
}
```

```output
12 / 3 = 4
```

## Two compilers, one fixed point

- `src/` is the C++ reference compiler: it parses, type-checks, and lowers Fin
  to machine code through LLVM, and it builds the first self-hosted stage.
- `finc/` is the compiler written in Fin. It bootstraps in three stages — the
  C++ compiler builds stage 1, stage 1 builds stage 2, stage 2 builds stage 3 —
  and CI requires the fixed point: `build/finc_stage2` and `build/finc_stage3`
  must be byte-identical. It lowers straight to LLVM IR, never to C
  ([ADR 0043](docs/adr/0043-self-hosting-compiler-lowers-to-llvm-ir.md)).

## Standard library

The shipped library is [`lib/std`](lib/std/); start with the
[library tour](docs/guide/12-standard-library-tour.md). Import spelling is
`from <module>::std`, except ambient `printf`. Importable does not mean
executable: resolving a signature does not by itself make a call build, so the
tour marks which paths run and which the backend still refuses.

## Where to go next

| Task | Start here |
| --- | --- |
| Give an agent enough context to write Fin | [Agent guide](docs/agent-guide.md) |
| Learn the language by topic | [Language guide](docs/guide/README.md) |
| Integrate an editor or build tool | [CLI and diagnostic contract](docs/finc-interface-contract.md) |
| Change the compiler | [Repository instructions](AGENTS.md) |
| Understand a language decision | [ADRs](docs/adr/) and [vocabulary](CONTEXT.md) |

The `.fin` files under `tests/samples/` are the language specification: each
carries a `//@` expectation (`ok`, `error`, or `unimplemented`), and a sample
without one is a harness fault, not a pass. Design documents and historical
drafts are not a list of implemented features.

## Contributing and testing

Build with `./build.sh` from the repository root, then run the suite:

```sh
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

To check every documentation example against your compiler (this file
included — each example above was built and run to produce its shown output):

```sh
python3 tests/tools/check_docs.py --finc build/finc
```

On Windows use `--finc build/finc.exe`. Keep compiler changes consistent with
the [machine contract](docs/finc-interface-contract.md): incompatible CLI
changes bump the contract version it defines.

## License

Fin is under the GNU General Public License v3 — see [LICENSE](LICENSE).

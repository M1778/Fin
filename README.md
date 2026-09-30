# Fin

Fin is a systems programming language. This repository contains its C++ compiler,
`finc`, and the Fin standard library in `lib/std`. The package manager, `finn`, is
a separate project.

**Writing a project in Fin with an AI agent? Give it
[the agent guide](docs/agent-guide.md) first.** It covers syntax, working patterns,
compiler limits, and the commands that distinguish checking code from running it.

```fin
fun main() <noret> {
    printf("Hello, Fin!\n");
}
```

```output
Hello, Fin!
```

With `finc` and a C compiler driver on `PATH`, save that as `hello.fin`:

```sh
finc hello.fin             # check syntax and types
finc hello.fin -o hello    # generate code and link
./hello
```

Fin is under development. Some constructs pass type-checking but cannot produce
machine code yet. Always build and run an application before treating it as done.

| Task | Start here |
| --- | --- |
| Give an agent enough context to write Fin | [Agent guide](docs/agent-guide.md) |
| Install prerequisites and build `finc` | [Quick start](docs/guide/01-quick-start.md) |
| Learn the language by topic | [Language guide](docs/guide/README.md) |
| Find library names and limitations | [Standard library](docs/guide/12-standard-library-tour.md) |
| Integrate an editor or build tool | [CLI and diagnostic contract](docs/finc-interface-contract.md) |
| Change the compiler | [Repository instructions](AGENTS.md) |
| Understand a language decision | [ADRs](docs/adr/) and [vocabulary](CONTEXT.md) |

For a source build on Linux or macOS, install a C++20 compiler, CMake, Conan 2,
Bison, Flex, and the LLVM development package pinned in `CMakeLists.txt`, then run
`./build.sh --release`. See the quick start for platform details.

To check the documentation examples against your compiler:

```sh
uv run --no-project tests/tools/check_docs.py --finc build/finc
```

On Windows, use `--finc build/finc.exe`; [native setup](docs/guide/01-quick-start.md#native-windows-with-uv) uses PowerShell and uv.

The language guide separates runnable programs, fragments, type-check-only
examples, and deliberate errors. The checker validates those labels and compares
the output shown beside runnable examples. Design documents and historical sample
drafts are not a list of implemented features.

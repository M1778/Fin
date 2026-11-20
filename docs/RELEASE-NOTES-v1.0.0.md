# Fin 1.0.0

Released 2026-11-20. `finc --version` answers `finc 1.0.0 (contract 3)`.

## What Fin is

Fin is a systems programming language with an LLVM backend: value types and classes, generics,
tagged-union enums, nullable values with an explicit denullifier, `blame` instead of exceptions,
compile-time `@special` functions, and macros returning one quoted expression. This repository holds the
reference compiler `finc` (C++ under `src/`), a self-hosting compiler written in Fin (`finc/`), and the
standard library `lib/std` (49 modules). `tests/samples/*.fin` is the specification, and each sample's
`//@` header is its testable expectation (75 samples).

## What 1.0 means

**The machine contract is frozen at 3.** Exit codes, stream discipline and the `--diagnostics=json` schema are
the stable interface tools branch on; semver and the contract integer move independently
([contract](finc-interface-contract.md), ADR 0009). **Six platforms are supported** — Linux, macOS and Windows on
x86_64 and aarch64, the required `.github/workflows/ci.yml` and release matrices, plus a static musl tarball — with
no silent gaps among them: `lib/std` selects Windows or POSIX declarations through the predefined `FIN_TARGET_*`
names, and the Windows filesystem and logging tests 0.4.x skipped now run there.

## Highlights

**Language and runtime.** `FIN_TARGET_WINDOWS`/`_DARWIN`/`_LINUX` and `FIN_TARGET_X86_64`/`_ARM64` are defined in
every file and imported module, so a library picks its native ABI declarations instead of refusing at the link
(ADR 0051 addendum). A non-capturing Fin function value can be passed where C wants a function pointer; unsupported
captures are refused by name. Self-hosted aggregate ownership gives struct bindings reusable storage, detaches
returns and recursively copies fixed arrays; assignment evaluates its receiver and right-hand side once, and an
explicit `cast<long>` from a negative `int` keeps its sign on both compilers.

**Standard library.** `fs`, `log`, `env`, `process`, `stdio`, `sync`, `networking`, `time`, `cli` and `path` gained
native Windows coverage — UCRT file I/O, Win32 directory enumeration, `_putenv_s`, Winsock 2,
`QueryPerformanceCounter`, native threads and SRW locks. Owners of memory gained explicit disposal
(`free_collection`, `free_hashmap`, `free_deque`/`free_queue`/`free_stack`,
`free_mutex`/`free_lock`/`free_channel`/`free_stream`, `free_command`, `free_parse_result`), and a weak
smart-pointer handle keeps the shared counters alive after the last strong release (ADR 0039 amendment). Repairs
with regressions: `walk_dir` branches per record shape, `copy_file` preserves embedded NUL bytes, `random_int`
covers the full inclusive `int` range, `base64_decode` rejects misplaced padding, `BTreeMap` blames instead of
calling a null comparator, `Collection` no longer over-allocates, `deadline_after_ms` refuses instead of wrapping,
and Windows paths recognize `\`, drive and UNC prefixes.

**Tooling.** `install.sh` compiles a released tag, never a moving branch, prints the commit it compiled, and refuses
when no release exists. Archives are named `finc-<version>-<target-triple>` and unpack to `bin/finc[.exe]` plus
`lib/std/**`; a preflight refuses to ship a toolchain with no standard library, and the native `.deb`, `.rpm`,
Windows setup exe and macOS `.dmg` come from that staged tree. `check_docs.py` compiles every Fin fence in the
guides, and ADR citations are checked.

## Known limitations

- **`fields_of` / `FieldInfo` do not exist**: blocked on a runtime owner for the field-name string — who allocates,
  returns and frees it (ADR 0057 note, `lib/std/reflect.fin`). The four scalar intrinsics and `derive_hash` do ship.
- **`derive_eq` / `derive_encode` do not exist**: gated on the open **R3** decision, whether filling a user-written
  `op ==` body from a derive call respects ADR 0036's "declared-only, never synthesized"
  (`docs/reflect-derive-plan.md` §4).
- **The wave-4 compiler API (`@implements`, `@defined`, `@Alloc`, `@Free`) is not shipped**: ADR 0042 is Proposed
  with plan-before-code addenda; `docs/compiler-api.md` records which operations exist.
- **The frontend accepts constructs the backend cannot lower**, so a file passing `finc file.fin` may still fail
  `finc file.fin -o app` — always by a named refusal. Windows narrow paths also use the system code page: UTF-8 and
  extended-length paths are not promised.
- **Self-hosting is proven on Linux** (Stage2/Stage3 compared there); non-Linux stages are outside bootstrap
  coverage. `log` emission is not thread-safe, `stdptr` copies do not increment counters, and `BTreeMap` insert is
  O(n).
- **`finn` in this workspace still speaks contract 2**, so the cross-repository contract check fails at version
  agreement; that does not affect `finc` (`docs/HANDOFF.md`).

## Install

Released builds ship `deb`, `rpm`, a Windows setup exe, a macOS `dmg`, portable tarballs and a static musl tarball,
each next to its `.sha256`, plus the `index.json` that is the authoritative list for the portable archives. From
source, read the script, then run it with POSIX `sh`:

```sh
curl -o install.sh https://raw.githubusercontent.com/M1778/Fin/master/install.sh
sh install.sh                          # newest released tag, or refuse
```

It clones `~/Fin` (override with `FIN_DIR`), checks your `llvm-config` against the LLVM major pinned in
`CMakeLists.txt` (22), runs `./build.sh --release --with-llvm --no-test`, smoke-tests one sample, and prints
`finc ready at: <dir>/build/finc`. You need a C and C++ compiler, CMake 3.20+, Conan 2, Bison 3.2+, Flex, Python 3
and the LLVM development package; `FIN_REF=<tag-or-full-commit>` pins what is compiled
([INSTALL.md](../INSTALL.md)).

## Verify a download

Every asset ships a `.sha256` sidecar — `sha256sum -c finc-1.0.0-<target>.tar.gz.sha256` — and `index.json` is
authoritative for the portable archives: `build_index.py` hashes the archive bytes rather than copying sidecars and
checks each sidecar against them, and `finn` reads `versions[<version>].targets[<triple>]`. `install.sh` is verified
by hand: the release notes `release.yml` generates carry its `sha256sum` at that tag, so compare after fetching
`https://raw.githubusercontent.com/M1778/Fin/<tag>/install.sh`; it does not verify its own bytes and no signature
exists yet ([SECURITY.md](../SECURITY.md)). The last check is the compiler itself — `finc --version` must print
`finc 1.0.0 (contract 3)`.

The historical `v0.4.0` tag is preserved and its release run did not publish. This tree is 1.0.0 (contract 3)
per `CMakeLists.txt` and `src/driver/Version.hpp`, and the annotated `v1.0.0` tag names commit `1aa4a8f`;
`release.yml` refuses any tag that is not `v<CMake version>`.

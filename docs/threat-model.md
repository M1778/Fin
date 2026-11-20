# Threat model — `finc`

Scope: the C++ reference compiler in `src/`, the shipped library in `lib/std/`, and the packaging
scripts that produce a release. `pyprototype/` and `legacy/` are historical and excluded (AGENTS.md).
This states what is defended and what is not — not that Fin is hardened.

## Assets

| Asset | Why an attacker wants it |
| --- | --- |
| Workstation running `finc` | Arbitrary native code execution at build time |
| Generated object / linked executable | Code the user then runs with their privileges |
| Release archives + `index.json` | What `finn` installs for everyone else |
| CI runners and caches | Publish artifacts, hold `GITHUB_TOKEN` |
| The compiler source tree | A poisoned `finc` poisons every later build |

## Trust boundaries

1. **`.fin` source → compiler.** Untrusted: it parses and generates code for bytes it did not author.
2. **Import text → filesystem.** Untrusted: `import "..."` and `--fin-libs` name paths.
3. **Compiler → external C driver.** Crossed with a command line built from `FIN_CC`, `FIN_LDFLAGS`, and user output paths.
4. **`lib/std/` → `@define` externs.** The stdlib is thin over libc; every extern is an unchecked call into the host ABI.
5. **CI → release consumers.**

**Attacker-controlled inputs:** `.fin` source (syntax, macros, comptime); import strings (relative,
absolute, `..`, symlinks); `--fin-libs` / `FIN_LIBS`; `@define` extern declarations; `FIN_CC`,
`FIN_LDFLAGS`, `PATH`; package content fetched by `finn`; release archives.

## Attack classes

### A. Malicious source → compiler memory unsafety
C++ over a hand-written lexer (`src/lexer/lexer.l`) and bison parser (`src/parser/parser.y`). A
malformed program that crashes the compiler is a real bug, and on a build server it is a denial of
service for everyone building. **Status: partially mitigated.** An ASan+UBSan sweep exists
(`.github/workflows/ci.yml:304`) but is non-blocking — `continue-on-error: true` at
`.github/workflows/ci.yml:307`. A libFuzzer harness for preprocessor/lexer/parser exists
(`fuzz/parse_fuzz.cpp:1`) but is off by default (`CMakeLists.txt:350`) and no CI workflow runs it.
Instrumentation exists; nothing enforces it.

### B. Path traversal via import resolution
`import "../../etc/passwd"` and `import "/etc/shadow"` are both accepted: resolution joins the import
text onto a base directory (`src/utils/ModuleLoader.cpp:143`) and accepts absolute paths outright
(`src/utils/ModuleLoader.cpp:150`). `fs::canonical` keys the module cache for identity, never confines
(`src/utils/ModuleLoader.cpp:205`).

**Status: no mitigation, and none intended today.** It reads whatever path the source names, as
`#include` does. The boundary that matters is *whose source you compile*: a `.fin` you did not write
is already arbitrary native code (see D), so traversal adds no capability the language does not grant.
What is genuinely missing is a **search path that cannot escape its root** — a sandboxed mode for
untrusted packages. Cycle detection (`src/utils/ModuleLoader.cpp:270`) addresses termination, not
confinement.

### C. Macro expansion and comptime as an escape hatch
**Status: mitigated for the current subset.** The interpreter's documented scope is literal threading
and closed operators, "no loops, no recursion" (`src/semantics/ComptimeInterp.hpp:38`). Recursion is
bounded at `kMaxDepth = 64` (`src/semantics/ComptimeInterp.hpp:212`), enforced by a depth check
returning a named gap (`src/semantics/ComptimeInterp.cpp:770`). Host reads
(`compiler.system.get_total_memory`, declared `src/semantics/CompilerApi.cpp:85`) fold to
**placeholders**, not real values, and are tainted so the branch warns
(`src/semantics/ComptimeInterp.cpp:629`). No comptime file or process I/O exists in `src/semantics/`.

**Caveat:** the *depth* bound is not a *step* bound. A macro expanding into many calls that each
expand is bounded per call, not in total work; `src/macros/expander/` declares no expansion budget.
Compile-time resource exhaustion by a malicious source is an open availability issue.

### D. Generated code and FFI hazards
`@define` declares a linker symbol with a hand-written signature. Nothing checks that the signature
matches the C function, and no Fin type is validated at the boundary — the stdlib's externs (e.g.
`lib/std/buffer.fin:11`) are trusted by construction. A malicious `.fin` calling `@define`s wrongly
gets undefined behaviour. **Status: no mitigation, by design** — the contract of C.

The one honest coding-integrity claim is the lowering invariant: an unlowerable construct refuses
instead of emitting guessed IR. `src/codegen/CodeGen_LLVM.cpp:65` records that "there is no path from
an unhandled node to a successful build", and every unlowerable node routes through `unsupported()`
at `src/codegen/CodeGen_LLVM.cpp:2254`. That guards against *silent wrong code*, not *deliberately
wrong code*. Crossing an `@define` boundary with an aggregate struct is likewise refused
(`src/codegen/CodeGen_LLVM.cpp:87`) — a correctness guard, not a security one.

### E. Command construction and the external driver
Linking shells out: on POSIX the command is assembled as a string and run through `std::system`
(`src/utils/Process.cpp:181`), each argument single-quoted first (`src/utils/Process.cpp:18`).
`FIN_CC` is read from the environment and becomes argv[0] (`src/utils/Process.cpp:188`).
**Status: mitigated for injection, not for trust.** Quoting stops an output filename becoming a
second command. It does not make `FIN_CC` safe: whoever controls the environment controls the linker,
so a hostile `FIN_CC` is code execution with no vulnerability required. `FIN_LDFLAGS` is
whitespace-split and passed through (`src/utils/Process.cpp:202`) under the same assumption. Set these
explicitly on shared machines.

### F. Supply chain of the compiler itself
*Release artifacts:* every asset gets a `<asset>.sha256` sidecar and a generated, non-hand-maintained
`index.json` (`.github/workflows/release.yml:327`). That answers "did the download arrive intact?"
**Nothing is signed** — no GPG, no Sigstore, no attestation — so it does not answer "did the right bytes
get published?". *Source installer:* `install.sh` pins the newest released tag and refuses rather than
cloning `master` (`install.sh:112`), printing the resolved commit; it verifies nothing about its own
bytes, and `FIN_REF` accepts any commit the user names. *CI:* actions are pinned to **major tags**
(`.github/workflows/ci.yml:63`), not commit SHAs, so a retagged action is silently accepted. Conan
packages are checksum-verified by Conan's own recipe hashes, not by anything Fin controls.
**Status: integrity without authenticity** — acceptable for development, insufficient for
high-assurance distribution.

## Known limitations

- No path confinement for imports; no sandboxed compile mode.
- No signed releases or build provenance; SHA-256 sidecars only. No SBOM published.
- ASan/UBSan CI is advisory and does not fail a build.
- The fuzzer is not wired into CI and covers only preprocessor/lexer/parser (`fuzz/parse_fuzz.cpp:8`); no semantic or codegen fuzzing exists.
- No expansion/step budget for macros; compile-time exhaustion is unresolved.
- Actions pinned to major tags rather than commit SHAs.
- The self-hosted bootstrap chain (`finc/`) is not reproducible-audited here; a poisoned earlier stage can propagate. Report stage-2/stage-3 divergence privately.

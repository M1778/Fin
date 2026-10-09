# Security Policy

## Supported Versions

Fin (`finc`) is pre-1.0 and under active development. Security fixes are
provided on the current release line only.

| Version | Supported |
| ------- | --------- |
| 0.4.x (current, `master`) | Yes |
| < 0.4.0 | No - please upgrade and re-test |

There are no LTS branches. If you are pinned to an older tree, the fix
will still land on `master` first; backports are best-effort at the
maintainer's discretion.

Identify your build with `finc --version` and compare against the
`project(... VERSION ...)` value in `CMakeLists.txt` at tip of `master`.

## Reporting a Vulnerability

**Do not open a public issue, discussion, or PR for a suspected
vulnerability.** Public reports put users at risk before a fix exists.

Report privately through GitHub's private vulnerability reporting:

1. Go to the **Security** tab of `M1778/Fin`.
2. Click **Report a vulnerability**
   (equivalently: `https://github.com/M1778/Fin/security/advisories/new`).
3. Fill in everything you can from the template below.

If private advisories are unavailable to you, open the most minimal
public issue possible - e.g. "please contact me about a security matter"
with a way to reach you - and **do not include any technical details**.
We will open a private channel from there.

### What to include

- Affected component(s) and version/commit (`finc --version` + git hash).
- Platform and toolchain (OS, LLVM major from `CMakeLists.txt`, C compiler
driver, `./build.sh` flags if non-default).
- Reproduction: minimal `.fin` input, build commands, observed vs.
expected behavior. For crashes include full stderr; with
`--diagnostics=json`, include the JSONL lines.
- Impact: what an attacker gains (code execution, file overwrite,
sandbox escape, supply-chain compromise, info leak, DoS) and what the
victim must do to trigger it (compile/run malicious `.fin`, run
`install.sh`, install a malicious `finn` package).
- Whether it also affects the self-hosted compiler (`finc/`), the stdlib
(`lib/std/`), or only the C++ reference compiler (`src/`).
- Suggested fix or mitigation, if you have one. Not required.

### What happens next

- **Acknowledgement:** we aim to confirm receipt within **7 days**.
- **Triage:** confirm, reproduce, and assess severity inside the private
advisory until a fix or a reasoned no-fix decision exists.
- **Fix and disclosure:** confirmed issues get a fix plus regression test
(per this repo's test-first rule), a release containing it, then disclosure
via a GitHub Security Advisory. Reporters who want credit get credit.
- **No-fix decisions** (not-a-vulnerability, upstream-only, requires
an already-untrusted input the user chose to run) will be explained in
the advisory thread, not silently closed.

There is no bug-bounty program. The reward is credit, our thanks, and a
safer compiler.

## Scope

In scope:

- `src/` - C++ reference compiler: driver, preprocessing, lexer, parser,
macro expansion, semantic analysis, LLVM lowering, diagnostics, module
loading (`ModuleLoader`), CLI contract behavior.
- `finc/` - self-hosted compiler in Fin (incl. bootstrap fixed-point:
stage tampering, non-reproducible stages).
- `lib/std/` - shipped standard library (esp. `fs`, `process`,
`networking`, `stdio`, `encoding`, FFI/`@define` boundaries).
- `install.sh`, `build.sh`, `conanfile.py`, CMake packaging, release
artifacts from `.github/workflows/release.yml`.
- `.github/workflows/` - CI supply chain (injection, artifact tampering,
cache poisoning, pages deployment).

Separate projects, route via us if unsure: `finn` (package manager) and
`finn-registry`. Report here and we will route, not bounce you.

Out of scope / historical (do not report as current-compiler issues):

- `pyprototype/` and `legacy/` - historical, not shipped, not evidence
of current behavior (see `AGENTS.md`).
- Third-party dependency bugs themselves (LLVM, Conan packages, system C
libs): report upstream, but tell us if Fin exposes them distinctively or
needs a version bump.
- `tests/samples/` aspirational cases and roadmap docs: design sketches,
not security boundaries.

### Safe harbor and ground rules

- Research against your own checkouts and artifacts. Do not attack GitHub
infra, other users, or the package registry.
- Do not exfiltrate data, disrupt CI, or publish exploits for unpatched
issues.
- Social engineering, spam, and DoS testing are out of scope.
- We will not pursue good-faith researchers who follow this policy.

## Security notes for users (please read)

Fin is a systems language with explicit memory management, raw pointers,
file/network/process access, and C FFI. Treat it accordingly:

- **The documented install is `curl ... | sh`.** That trusts this repo, its
hosting, and your network on every run. Prefer a pinned checkout you have
inspected (`git clone` + verify + read `install.sh`), esp. on shared/CI
machines.
- **Never compile-and-run untrusted `.fin` sources blindly.** A program you
build and execute has native power: files, sockets, processes. Review
first; run unfamiliar programs containerized with restricted network.
- **Compiler bugs are attack surface.** `finc` parses complex input and
shells out to a C driver for linking (`FIN_CC`). Keep LLVM and your C
compiler patched, build from a clean tree (`git status` showing only
intended changes), and be suspicious of diagnostics asking you to run
extra commands.
- **Bootstrapping:** a compromised stage-1 can poison later stages.
Rebuild from the C++ reference compiler when in doubt; report any
stage-2 vs stage-3 mismatch privately - it may be a correctness bug, but
treat it as security-relevant until ruled out.
- **Environment matters:** `FIN_LIBS`, `FIN_CC`, and `PATH` change what gets
compiled and linked. Set them explicitly on multi-user machines.

## Disclosure history

Confirmed advisories will be published under
`https://github.com/M1778/Fin/security/advisories` once fixed. No entries
there means none published, not none possible - keep current.

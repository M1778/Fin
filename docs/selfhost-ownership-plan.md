# Self-hosted aggregate value ownership

Status: local repair acceptance verified on Linux (2026-10-23). This document
records a compiler repair, not a change to Fin ownership or destructor rules. The
C++ compiler and its executable value-copy regressions define the required behavior.

## Observed defect

The baseline stage compiler represented both a struct value and a pointer to a
struct as a pointer to a heap buffer. Ordinary struct literals allocated 1,024 bytes.
Declarations, assignments, and returns forwarded that pointer; scope cleanup invoked
declared destructors but did not release the representation buffer. The sync
lifecycle probe consequently leaked 1,028 `Outcome<int>` buffers and three reported
`Thread` buffers: 1,055,744 bytes in LeakSanitizer.

Adding `free` after calls is unsound. A function returning `*p` formerly forwarded
the caller's pointer. A binding initialized from a call formerly kept its returned pointer.
A method on a temporary can return a pointer to a field which a later statement
uses; the reference compiler keeps that receiver in caller-frame storage.

## Chosen implementation direction

Preserve the internal pointer calling convention while separating language values
from explicit heap pointers. A struct value has a heap address, but ordinary local
storage has **frame lifetime**: an entry allocation per source storage site, reused
when that site runs in a loop. Each binding has its own home. Assignment writes
into that home rather than changing its address. Nested value fields have separate
homes; array handles and pointer fields still share user-owned payloads.

A value return always snapshots into detached heap storage before semantic
cleanup. Its caller copies into a caller-frame home and releases only the detached
representation, not its payload handles or semantic resources. Absence remains a
null result through both operations. A return of `&S` stays borrowed. The caller's
temporary home survives subsequent statements, including a method call returning
a pointer into that temporary.

Frame-lifetime heap boxes replace the earlier heap-only/fresh-result transfer
experiment. A raw frame alloca cannot escape in a returned nested field; a detached
snapshot must own its complete representation tree. Reusing entry homes also avoids
one allocation per loop iteration and preserves addresses taken before reassignment.

Array elements written as struct values need storage owned by their **buffer**, not
by the writer's frame. First stores snapshot; later stores update the element's
home. Deleting the array releases element representation before its user-owned
buffer. The LLVM C argument-table carriers keep their existing inline contiguous
pointer arrays; ordinary fixed-array fields use separate owned buffers.

The snapshot and store operations are type-directed. Nested struct value fields
copy recursively; pointers, strings, and dynamic array handles retain their existing
sharing and explicit deallocation rules. Fixed array values copy their elements.
Copies use the declared representation and never blindly read 1,024 bytes from a
borrowed pointer. A detached return snapshot must own all of its nested aggregate
representation storage so the caller can release it without releasing user-owned
payloads.

Explicit `new S{...}` must remain distinguishable from `S{...}` in the AST. Heap
objects retain explicit ownership and valid nested value storage. Taking the address
of a value borrows its stable storage; it does not create a second owner. Globals
retain static storage duration. Unknown or erased shapes which cannot satisfy this
contract must refuse explicitly rather than guess an ownership rule.

Representation storage and semantic destruction are separate. Existing destructor
composition, move behavior, and `#[slaveof]` rules determine when semantic cleanup
runs. Representation changes must neither invoke an extra destructor on a transfer
nor suppress an existing one. Pinned and deferred values must retain storage for
their required lifetime.

## Public regression seams

The authorized seams are compiler CLI compilation and observable behavior of its
emitted executable. Tests live in `tests/test_stage_value_ownership.cpp` and compare
Reference, Stage2, and Stage3 at `-O0` and `-O2`. Linux emitted-program leak checks
link with LeakSanitizer; allocator-error probes link with AddressSanitizer. These
links do not instrument arbitrary emitted loads/stores. Tests do not depend on
compiler-internal helper names or IR.

## Phased acceptance

1. **Independent values.** A borrowed struct snapshot, local copy, parameter copy,
   reassignment, and nested value-field copy preserve the original. Pointer fields
   keep sharing their pointee. Each vertical slice first demonstrates the stage
   failure and reference behavior.
2. **Storage ownership.** Repeated value construction, returns, discarded results,
   and reassignment leave no hidden aggregate leaks. Explicit heap allocation and
   deletion remain valid. Temporary receiver field pointers remain usable from
   later statements in the same frame.
3. **Control flow and language cleanup.** Branches, early returns, loop exits,
   destructor composition, moves, and tied lifetimes retain reference behavior.
   Generic concrete value types use the same ownership rules as ordinary structs.
4. **Integrated proof.** The sync producer/consumer lifecycle is leak-clean with
   stage-generated code. Existing array, generic, destructor, and stage-agreement
   regressions pass. Scratch stage2/stage3 converge before root runs the canonical
   bootstrap and full release gates.

Each phase is incomplete until its public regressions pass. A passing bootstrap or
ordinary runtime test does not substitute for ownership and leak checks. If the
pointer convention requires more special cases than native LLVM aggregates, revise
this implementation direction with evidence before extending it.

## Verification progress (2026-10-23)

- Twenty-one public programs compare Reference, Stage2, and Stage3 at `-O0` and
  `-O2`; their final focused CTest gate passed **63/63 tests** (14.15 seconds).
  Separate allocator-sanitizer runs passed **126/126 executions** with
  AddressSanitizer-linked objects. Leak detection was enabled except for the
  intentionally pinned lexical-lifetime program. These are allocator-error/leak
  checks, not full instrumentation of emitted LLVM memory accesses.
- The shared-channel probe's baseline **1,055,744 bytes / 1,031 allocations**
  is eliminated in both final stages; LeakSanitizer and AddressSanitizer-linked
  executions return zero. This is a program-ownership result, not a claim that
  the stage compiler process itself is leak-free.
- Probes cover repeated construction, returns, discarded calls, reassignment and
  borrowed-address stability, nested defaults, explicit deletion, buffer-owned
  array elements, scalar and struct elements in fixed-array fields, null results,
  early returns, loop exits, destructor composition, tied moves, pinned lexical
  storage, zero reinitialization, pointer stores, and static global assignment.
- Final scratch Stage2/Stage3 are byte-identical (SHA-256
  `38815b96a91639008fe24b1cb71153313883475379934905518d726419f683aa`).
- The final same-source full gate passed **3,036 tests, 57 skipped, zero
  failures** (3,093 checks, 273.30 seconds). The four local acceptance phases
  above are verified by their public regressions and this integrated gate.
- Documentation verification passed **74 examples**, with six explicitly marked
  fragments skipped. Formatting and `git diff --check` passed.
- Cross-frame pointers to pinned locals are not a reference-backed promise:
  allocator churn invalidated that shape in the C++ backend. Pinned representation
  is not freed at ordinary frame exit; lexical-scope lifetime is covered.

Local repair acceptance is complete for the tested ownership shapes. Native
platform CI and the remaining release gates are separate. This repair does not
remove unrelated backend refusals or establish new ownership language rules.

## Empty-handle follow-up (2026-10-23)

Broader installed Collection/HashMap lifecycle probes found additional leaks outside
the earlier aggregate-box probes: 512 bytes / 32 allocations and 6,144 bytes /
384 allocations respectively. Their no-initializer dynamic arrays now lower to null
handles, not hidden empty allocations. Length and deletion handle null without
reading a nonexistent allocation header. Three public cross-stage regressions pin
the null/length behavior and repeated container cleanup.

- **24 public programs / 72 parameterized tests** compare the three compilers at
  `-O0` and `-O2`; the focused gate passed **72/72 tests** (16.79 seconds).
- **144/144 allocator-sanitizer executions passed**, excluding leak detection only
  for intentionally pinned lexical storage.
- Final Debug: **3,045 passed, 57 skipped, zero failures** (3,102 checks,
  319.47 seconds). Final Release: the same counts (259.25 seconds).
- Final full ASan/UBSan compiler suite: **2,814 passed, 299 skipped, zero failures**
  (3,113 checks, 866.76 seconds), with leak detection and halt-on-error enabled.
  This build has no stage executables; stage allocator checks are separate.
- Both reference builds bootstrap identical Stage2/Stage3 bytes (SHA-256
  `1415dbd213825a98a30594c943b6d34bbf16f56904750d7412dcfb78d09a276c`).
- Static Alpine/musl: **2,802 passed, 300 skipped, zero failures** (3,102 checks,
  135.94 seconds); this lane has no stage executables. Its installed documentation,
  architecture and no-dynamic-loader gates passed.

Earlier validation counts above describe the aggregate candidate; this final
follow-up supersedes its compiler/test result. Native platform CI and publishing
remain separate and have not been run for the uncommitted revision.

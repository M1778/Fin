# ADR 0043: Self-hosting compiler lowers to LLVM IR, refusing C generation

**Date**: 2026-09-21
**Status**: accepted
**Deciders**: M1778, Antigravity

## Context

The Fin compiler (`finc`) is being written in the Fin programming language to achieve self-hosting.
Fin's core compiler architecture invariant (ADR 0002) mandates a native LLVM backend; the language has
no foreign runtime and does not rely on foreign language transpilation.

Earlier prototyping explored emitting intermediate C source files and invoking an external C compiler
for the bootstrap driver. Generating C code violates Fin's language identity, bypasses the LLVM IR
pipeline, and introduces an unwanted dependency on host C compiler toolchains. Fin compilation must
occur strictly and solely via Intermediate Representation and LLVM.

Furthermore, the existing Fin compiler provides seamless interoperability with C/C++ libraries and ABIs
via `#[llvm_name="..."]` and `@define` declarations. LLVM natively provides a stable, C-linkable interface
(`llvm-c`) exposed by `libLLVM.so`.

## Decision

We decide:

1. **C generation is forbidden**: `finc` written in Fin will not generate C source code for compilation.
   Any intermediate code generation must be LLVM Intermediate Representation (IR).
2. **Direct LLVM C API Lowering**: The self-hosting compiler lowers Fin ASTs directly to in-memory LLVM IR
   using the LLVM C API (`llvm-c`) via `@define` extern declarations:
   - Module management: `LLVMModuleCreateWithName`, `LLVMDisposeModule`, `LLVMDumpModule`
   - Context & Types: `LLVMInt32Type`, `LLVMInt64Type`, `LLVMInt8Type`, `LLVMPointerType`, `LLVMFunctionType`, `LLVMVoidType`, `LLVMStructType`
   - IR Builder: `LLVMCreateBuilder`, `LLVMPositionBuilderAtEnd`, `LLVMBuildRet`, `LLVMBuildAdd`, `LLVMBuildSub`, `LLVMBuildMul`, `LLVMBuildICmp`, `LLVMBuildBr`, `LLVMBuildCondBr`, `LLVMBuildAlloca`, `LLVMBuildLoad2`, `LLVMBuildStore`, `LLVMBuildCall2`
   - Functions & Blocks: `LLVMAddFunction`, `LLVMAppendBasicBlock`
   - Target & Emission: `LLVMInitializeAllTargets`, `LLVMInitializeAllTargetMCs`, `LLVMInitializeAllAsmPrinters`, `LLVMTargetMachineEmitToFile`, `LLVMWriteBitcodeToFile`
3. **Module Architecture**:
   - `finc/llvm.fin`: Encapsulates LLVM-C API types (`LLVMModuleRef`, `LLVMValueRef`, `LLVMTypeRef`, `LLVMBuilderRef`, etc.) and `@define` extern function bindings.
   - `finc/codegen.fin`: Lowers Fin AST statements and expressions directly into LLVM IR instructions using `finc/llvm.fin`.
   - `finc/driver.fin`: Drives module parsing, type checking, LLVM codegen, and target machine object/binary emission.

## Alternatives Considered

### Alternative 1: C Source Code Generation
- **Pros**: Easy to emit formatted text strings.
- **Cons**: Violates ADR 0002; loses control over LLVM optimization passes; requires host C compiler and headers; introduces translation semantics that drift from Fin's formal specification.
- **Why rejected**: Incompatible with project invariants. Fin is a systems language with an LLVM backend, not a transpiler.

### Alternative 2: Textual LLVM IR (.ll) Emitter
- **Pros**: Human-readable text IR output.
- **Cons**: Requires extensive string formatting for SSA temporaries and type annotations; requires disk I/O and external `llc`/`clang` process execution; misses programmatic LLVM verification and direct in-memory API diagnostics.
- **Why rejected**: The LLVM C API allows programmatic type checking, constant folding, and direct object emission without subprocess shell overhead.

### Alternative 3: Compiler Intrinsics in C++ Compiler
- **Pros**: Custom helper functions tailored specifically to Fin.
- **Cons**: Binds the self-hosting compiler to non-standard, internal C++ compiler builtins rather than standard public LLVM interfaces, hindering long-term compiler portability.
- **Why rejected**: The standard LLVM C API is already installed and linkable on Linux systems.

## Consequences

### Positive
- Strict adherence to the LLVM-only compiler architecture (ADR 0002).
- Clean, type-safe Fin bindings to LLVM via `finc/llvm.fin`.
- Direct binary emission (`.o`, `.bc`, ELF) without invoking external C compilers.
- Preserves full access to LLVM optimization passes and target machine code generation.

### Negative
- Requires declaring LLVM-C function prototypes in `finc/llvm.fin`.
- Requires linking the resulting `finc` binary against `libLLVM.so` (or static LLVM libraries).

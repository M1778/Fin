cmake_minimum_required(VERSION 3.20)
project(FinCompiler VERSION 0.4.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- C++ standard -------------------------------------------------------------
# The Conan toolchain sets CMAKE_CXX_STANDARD from `compiler.cppstd` in the
# profile (see conan/profiles/fin, which pins gnu20).  When configuring without
# Conan, fall back to 20 rather than to the compiler default, which is 17 for
# g++ 13 and does not compile this tree.
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_STANDARD LESS 20)
    message(FATAL_ERROR
        "finc requires C++20; CMAKE_CXX_STANDARD is ${CMAKE_CXX_STANDARD}. "
        "If you are using Conan, install with -pr:a=conan/profiles/fin.")
endif()

# --- LLVM (ADR 0010: one LLVM major, proven by CI) ----------------------------
# FIN_LLVM_MAJOR is the single major version finc is built against, on every
# platform.  It is enforced rather than merely documented: a mismatched LLVM is
# a configure error, not a warning, because "the two are different compilers
# wearing the same version number" is precisely the failure ADR 0010 exists to
# stop.
#
# FIN_WITH_LLVM is ON from wave 5 onwards -- this is the switch the comment above
# said would flip, and src/codegen/CodeGen_LLVM.cpp is what flipped it.  With it
# OFF the build still succeeds and finc still checks: src/codegen/CodeGen_Stub.cpp
# takes the module's place and every `-o` refuses with "this build has no
# backend", which is a diagnostic and not a silently empty artifact.  That is the
# configuration for a platform where LLVM 18 cannot be had -- ConanCenter
# publishes no LLVM 18 for Linux armv8, macOS x86_64 or Windows armv8, and none
# of the six CI combinations may be allowed to go quiet over it.
set(FIN_LLVM_MAJOR 18 CACHE STRING
    "The single LLVM major version finc is built against (ADR 0010)")
option(FIN_WITH_LLVM
    "Find and link LLVM ${FIN_LLVM_MAJOR}. Required from wave 5 (codegen)." ON)

# --- Dependencies ---
find_package(BISON 3.2 REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt CONFIG REQUIRED)

if(FIN_WITH_LLVM)
    # No version argument, deliberately.  LLVMConfigVersion.cmake compares
    # major.minor, so asking for `18` leaves PACKAGE_FIND_VERSION_MINOR empty and
    # LLVM 18.1.3 is *rejected* as incompatible with 18 -- which is the opposite
    # of what the pin means.  The check below is the pin, and it is the honest
    # place for it: it compares the major that ADR 0010 names against the major
    # that was found, and it says which one it found and where.
    find_package(LLVM REQUIRED CONFIG)
    if(NOT LLVM_VERSION_MAJOR VERSION_EQUAL FIN_LLVM_MAJOR)
        message(FATAL_ERROR
            "ADR 0010: finc is built against exactly one LLVM major, "
            "${FIN_LLVM_MAJOR}. Found ${LLVM_PACKAGE_VERSION} at "
            "${LLVM_DIR}.")
    endif()
    message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")
else()
    message(STATUS "LLVM not linked (FIN_WITH_LLVM=OFF); pin is LLVM ${FIN_LLVM_MAJOR}")
endif()

# --- Generate Sources ---
BISON_TARGET(Parser src/parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
    COMPILE_FLAGS "-d -Wcounterexamples"
)
FLEX_TARGET(Lexer src/lexer/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# --- Core Library (The Compiler Engine) ---
add_library(fin_core STATIC
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}

    # Diagnostics & Driver
    src/diagnostics/DiagnosticEngine.cpp
    src/driver/Driver.cpp
    src/utils/ModuleLoader.cpp

    # AST Base & Utilities
    src/ast/ASTNode.cpp
    src/ast/ASTPrinter.cpp
    src/ast/CloneVisitor.cpp
    src/ast/NodeKind.cpp
    src/ast/StructuralWalk.cpp

    # AST Nodes: Expressions
    src/ast/exprs/ArrayExpr.cpp
    src/ast/exprs/BinaryOp.cpp
    src/ast/exprs/FunctionCall.cpp
    src/ast/exprs/Identifier.cpp
    src/ast/exprs/Lambda.cpp
    src/ast/exprs/Literal.cpp
    src/ast/exprs/MiscExpr.cpp
    src/ast/exprs/PrototypeExpr.cpp
    src/ast/exprs/StructureExpr.cpp
    src/ast/exprs/UnaryOp.cpp

    # AST Nodes: Declarations
    src/ast/decls/ClassDecl.cpp
    src/ast/decls/DefineDecl.cpp
    src/ast/decls/FunctionDecl.cpp
    src/ast/decls/ImplementsBlock.cpp
    src/ast/decls/MacroDecl.cpp
    src/ast/decls/Program.cpp
    src/ast/decls/StructDecl.cpp
    src/ast/decls/TypeDef.cpp

    # AST Nodes: Statements
    src/ast/stmts/ControlFlow.cpp
    src/ast/stmts/ErrorHandling.cpp
    src/ast/stmts/Import.cpp
    src/ast/stmts/Statement.cpp
    src/ast/stmts/VariableDecl.cpp

    # AST Nodes: Types & Other
    src/ast/types/Attribute.cpp
    src/ast/types/GenericParam.cpp
    src/ast/types/TypeNode.cpp
    src/ast/nodes/Parameter.cpp

    # AST Cloning Implementations
    src/ast/cloning/CloneDecls.cpp
    src/ast/cloning/CloneExprs.cpp
    src/ast/cloning/CloneStmts.cpp
    src/ast/cloning/CloneTypes.cpp

    # Semantic Analysis
    src/semantics/CompilerApi.cpp
    src/semantics/impl/Analyzer_CompilerApi.cpp
    src/semantics/impl/Analyzer_Core.cpp
    src/semantics/impl/Analyzer_Decl.cpp
    src/semantics/impl/Analyzer_Stmt.cpp
    src/semantics/impl/Analyzer_Expr.cpp

    # Types System
    src/types/Type.cpp
    src/types/ErrorType.cpp
    src/types/DynamicType.cpp
    src/types/ArrayType.cpp
    src/types/FunctionType.cpp
    src/types/GenericType.cpp
    src/types/PointerType.cpp
    src/types/NullableType.cpp
    src/types/PrimitiveType.cpp
    src/types/SelfType.cpp
    src/types/StructType.cpp

    # Macros & Preprocessor
    src/preprocessor/Preprocessor.cpp
    src/macros/MacroExpander.cpp
    src/macros/SubstitutionVisitor.cpp
    src/macros/expander/ExpanderDecls.cpp
    src/macros/expander/ExpanderExprs.cpp
    src/macros/expander/ExpanderStmts.cpp
    src/macros/expander/ExpanderTypes.cpp
)

# --- Backend (wave 5) ---------------------------------------------------------
# Exactly one of these two is compiled, and both implement the whole of
# src/codegen/CodeGen.hpp.  The stub is not a no-op: it refuses, so a build
# without LLVM produces diagnostics rather than empty artifacts.
if(FIN_WITH_LLVM)
    target_sources(fin_core PRIVATE src/codegen/CodeGen_LLVM.cpp)
else()
    target_sources(fin_core PRIVATE src/codegen/CodeGen_Stub.cpp)
endif()

target_link_libraries(fin_core PUBLIC fmt::fmt)

if(FIN_WITH_LLVM)
    target_link_libraries(fin_core PUBLIC LLVM)
    target_include_directories(fin_core PUBLIC ${LLVM_INCLUDE_DIRS})
    # PUBLIC so tests/CMakeLists.txt can key its backend tests off the same
    # switch without repeating the find_package result.
    target_compile_definitions(fin_core PUBLIC FIN_WITH_LLVM=1)
endif()

target_include_directories(fin_core PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

# One source of truth for the version: PROJECT_VERSION on line 2.  The release
# workflow reads it from here when it names the archives, conanfile.py's
# set_version() reads it from here, and src/driver/Version.hpp consumes this
# macro -- so a version bump is a one-line edit and a stale literal anywhere
# else is a build error rather than a mislabelled release.
#
# PUBLIC on fin_core, so it reaches finc and fin_tests through the link without
# either of them naming it.  The macro is a string literal: `FINC_VERSION` is
# `"0.4.0"`, not `0.4.0`.
target_compile_definitions(fin_core PUBLIC FINC_VERSION="${PROJECT_VERSION}")

if(MSVC)
    # flex's generated scanner reaches for POSIX headers; lexer.l already sets
    # `%option nounistd`, this covers the generated parser's siblings.
    target_compile_definitions(fin_core PUBLIC YY_NO_UNISTD_H)
    # /utf-8: the sources contain UTF-8 literals and MSVC otherwise reads them
    # in the active code page.  /bigobj: parser.cpp is 300k lines of tables.
    target_compile_options(fin_core PUBLIC /utf-8 /bigobj)
    target_compile_definitions(fin_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# --- CLI Executable ---
add_executable(finc src/main.cpp)
target_link_libraries(finc PRIVATE fin_core)

# --- Install / package layout (ADR 0010) --------------------------------------
# A release archive unpacks to exactly `bin/finc[.exe]` and `lib/std/**`, with
# no top-level version directory: `finn` supplies the versioned parent.
install(TARGETS finc RUNTIME DESTINATION bin)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/std)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/std
            DESTINATION lib
            FILES_MATCHING PATTERN "*.fin")
else()
    message(WARNING
        "lib/std/ does not exist, so `cmake --install` produces an archive with "
        "no standard library. The stdlib track owns moving lib/std into place; "
        "the release workflow fails rather than shipping without it.")
endif()

# --- Testing ---
# The release workflow configures with -DFIN_BUILD_TESTS=OFF: a release archive
# contains bin/finc and lib/std and nothing from tests/, so it has no business
# depending on the test harness building.
option(FIN_BUILD_TESTS "Build the test suite (needs GTest)." ON)
if(FIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

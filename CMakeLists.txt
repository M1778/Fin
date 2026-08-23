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
# FIN_WITH_LLVM defaults OFF because no translation unit in src/ includes an
# LLVM header today (`grep -rn '#include *[<"]llvm' src` finds nothing) and
# ConanCenter publishes no LLVM 18 for any platform, nor any llvm-core binary
# at all for Linux armv8, macOS x86_64 or Windows armv8.  Turning it ON with a
# system LLVM 18 present is supported and checked; wave 5 (codegen) is where
# the switch flips and where the binaries have to come from somewhere.
set(FIN_LLVM_MAJOR 18 CACHE STRING
    "The single LLVM major version finc is built against (ADR 0010)")
option(FIN_WITH_LLVM
    "Find and link LLVM ${FIN_LLVM_MAJOR}. Required from wave 5 (codegen)." OFF)

# --- Dependencies ---
find_package(BISON 3.2 REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt CONFIG REQUIRED)

if(FIN_WITH_LLVM)
    find_package(LLVM ${FIN_LLVM_MAJOR} REQUIRED CONFIG)
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

target_link_libraries(fin_core PUBLIC fmt::fmt)

if(FIN_WITH_LLVM)
    target_link_libraries(fin_core PUBLIC LLVM)
    target_include_directories(fin_core PUBLIC ${LLVM_INCLUDE_DIRS})
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

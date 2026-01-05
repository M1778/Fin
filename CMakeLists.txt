cmake_minimum_required(VERSION 3.20)
project(FinCompiler VERSION 0.1.0 LANGUAGES CXX C)

# set(CMAKE_CXX_STANDARD 20) -> conan handles this
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM targets: ${LLVM_AVAILABLE_LIBS}")
message(STATUS "LLVM INCLUDE DIR: ${LLVM_INCLUDE_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})

# NO NEED FOR SYSTEM-INSTALLED LLVM
# add_definitions(${LLVM_DEFINITIONS})
# llvm_map_components_to_libnames(llvm_libs core native support bitwriter irreader)
# llvm_map_components_to_libnames(LLVM_LIBS  Core  Support  IRReader)


# --- Generate Sources ---
BISON_TARGET(Parser src/parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
    COMPILE_FLAGS "-d -Wcounterexamples"
)
FLEX_TARGET(Lexer src/lexer/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# add_dependencies(FLEX_Lexer_OUTPUTS BISON_Parser_OUTPUTS)

# --- Core Library (The Compiler Engine) ---
add_library(fin_core STATIC
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
    src/ast/ASTPrinter.cpp
    src/diagnostics/DiagnosticEngine.cpp
    src/ast/ASTNode.cpp
    src/preprocessor/Preprocessor.cpp
    src/semantics/impl/Analyzer_Core.cpp
    src/semantics/impl/Analyzer_Decl.cpp
    src/semantics/impl/Analyzer_Stmt.cpp
    src/semantics/impl/Analyzer_Expr.cpp
    src/driver/Driver.cpp 
    src/macros/MacroExpander.cpp
    src/ast/CloneVisitor.cpp
    src/macros/SubstitutionVisitor.cpp
    src/types/Type.cpp
    src/utils/ModuleLoader.cpp 
)


target_link_libraries(fin_core PUBLIC 
    fmt::fmt 
    LLVM
    ${llvm_libs}
)

target_include_directories(fin_core PUBLIC 
    src 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LLVM_INCLUDE_DIRS}
)

# --- CLI Executable ---
add_executable(finc src/main.cpp)

target_link_libraries(finc PRIVATE fin_core)

# --- Testing ---
enable_testing()
add_subdirectory(tests)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

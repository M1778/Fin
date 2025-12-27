cmake_minimum_required(VERSION 3.20)
project(FinCompiler VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt REQUIRED)
find_package(LLVM REQUIRED CONFIG)
find_package(GTest REQUIRED) # Added for testing

add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs core native support bitwriter irreader)

# --- Generate Sources ---
BISON_TARGET(Parser src/parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
    COMPILE_FLAGS "-d"
)
FLEX_TARGET(Lexer src/lexer/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
add_dependencies(FLEX_Lexer_OUTPUTS BISON_Parser_OUTPUTS)

# --- Core Library (The Compiler Engine) ---
add_library(fin_core STATIC
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Lexer_OUTPUTS}
    # Add other .cpp files here later (e.g., src/ast/AST.cpp)
)

target_link_libraries(fin_core PUBLIC 
    fmt::fmt 
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
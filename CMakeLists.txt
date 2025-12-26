cmake_minimum_required(VERSION 3.20)
project(FinCompiler VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies ---
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(fmt REQUIRED)
find_package(LLVM REQUIRED CONFIG)

add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs core native support bitwriter irreader)

# --- Flex/Bison Setup ---
# Generate the Lexer C++ file from lexer.l
FLEX_TARGET(Lexer src/lexer/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)

# --- Executable ---
add_executable(finc 
    src/main.cpp
    ${FLEX_Lexer_OUTPUTS} # Include the generated lexer.cpp
)

target_link_libraries(finc PRIVATE 
    fmt::fmt 
    ${llvm_libs}
)

target_include_directories(finc PRIVATE 
    src 
    ${CMAKE_CURRENT_BINARY_DIR} # To find generated files
    ${LLVM_INCLUDE_DIRS}
)

target_include_directories(finc PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer"
)

if(MSVC)
    target_compile_options(finc PRIVATE /W4)
else()
    target_compile_options(finc PRIVATE -Wall -Wextra -Wpedantic)
endif()

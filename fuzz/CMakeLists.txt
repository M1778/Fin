# libFuzzer harness (issue #84). Reached only with -DFIN_FUZZ=ON, which the top
# level gates before this directory is entered.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "FIN_FUZZ needs Clang: libFuzzer is -fsanitize=fuzzer, which GCC does "
        "not implement. Configure with -DCMAKE_CXX_COMPILER=clang++.")
endif()

add_executable(fin_parse_fuzz parse_fuzz.cpp)
# fin_core's PUBLIC includes carry parser.hpp (generated) and the LLVM headers.
target_link_libraries(fin_parse_fuzz PRIVATE fin_core)
# -fuzzer here; address+undefined ride along so a bare FIN_FUZZ build still has
# a sanitizer on the harness TU. Add -DFIN_SANITIZE=ON for full fin_core
# instrumentation (the two options compose: FIN_SANITIZE adds the same pair
# globally, this file only adds fuzzer).
target_compile_options(fin_parse_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
target_link_options(fin_parse_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined)

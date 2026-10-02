add_executable(fintest main.cpp)

# Same absolute-path pattern as tests/CMakeLists.txt: the runner must find the
# just-built finc regardless of the working directory it is launched from.
target_compile_definitions(fintest PRIVATE
    FINC_BINARY="$<TARGET_FILE:finc>"
)

add_dependencies(fintest finc)

# Slice 2: per-file ctest registration over the seed corpus.
# The root enables testing after this subdirectory (R1-A6 block, do not move),
# so enable it here for this directory and below.
enable_testing()
# file(GLOB) runs at configure time: re-run cmake after adding or removing
# `fintest/**/*_test.fin` files. Scans only the seed dir, never tests/
# (CorpusDiscovery guard: tests/ is owned by the gtest harness, ADR 0008).
file(GLOB_RECURSE FINTEST_FILES "${CMAKE_SOURCE_DIR}/fintest/*_test.fin")
foreach(f IN LISTS FINTEST_FILES)
    file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}/fintest" "${f}")
    string(REGEX REPLACE "\\.fin$" "" name "${rel}")
    string(REPLACE "/" "-" name "${name}")
    add_test(NAME "fintest:${name}" COMMAND fintest --file "${f}")
endforeach()

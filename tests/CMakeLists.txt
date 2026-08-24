find_package(GTest REQUIRED)

# Define the test executable here (and ONLY here)
add_executable(fin_tests
    test_parser.cpp
    test_expectations.cpp
    test_cli.cpp
    test_semantics.cpp
    test_soundness.cpp
    test_codegen.cpp
    test_layout.cpp
    test_stdlib.cpp
    test_module_loader.cpp
    test_macro_expander.cpp
    Corpus.cpp
    Pipeline.cpp
)

target_link_libraries(fin_tests PRIVATE
    fin_core
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(fin_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The harness reads the corpus and the finc binary through absolute paths baked
# in at configure time. The old suite discovered samples with a relative path
# during static initialisation, so a wrong working directory registered zero
# tests and the run reported success (ADR 0008).
target_compile_definitions(fin_tests PRIVATE
    FIN_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FIN_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
    FINC_BINARY="$<TARGET_FILE:finc>"
)

# test_codegen.cpp compiles and *runs* what finc produced, which only exists in a
# build that has a backend.  Without it those tests skip with a reason rather
# than disappearing from the count -- a suite that silently loses 25 tests when a
# switch flips is a suite that cannot tell you the backend regressed.
if(FIN_WITH_LLVM)
    target_compile_definitions(fin_tests PRIVATE FIN_TESTS_HAVE_BACKEND=1)
endif()

# The expectation runner shells out to finc, so the binary must exist before the
# tests run — including before gtest_discover_tests enumerates them.
add_dependencies(fin_tests finc)

include(GoogleTest)
gtest_discover_tests(fin_tests)

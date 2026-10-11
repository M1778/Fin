add_executable(finfmt main.cpp)
target_link_libraries(finfmt PRIVATE fin_core)

# Slice 1: the binary plus its own enforcement tests, all inside this
# directory (cf. tools/fintest/CMakeLists.txt). No ci.yml change is needed:
# ctest already runs every registered test.
enable_testing()

# Idempotency + canonical-style gate over the corpus the tool owns: fails
# until `finfmt` has been run over lib/std and stays failing on any drift.
file(GLOB FINFMT_LIBSTD_FILES "${CMAKE_SOURCE_DIR}/lib/std/*.fin")
add_test(NAME "finfmt:check-libstd"
    COMMAND $<TARGET_FILE:finfmt> --check ${FINFMT_LIBSTD_FILES})

# Issue #77: the same gate over the self-host compiler sources and the sample
# corpus. Separate named tests so a failure names which corpus drifted.
file(GLOB FINFMT_SELFHOST_FILES "${CMAKE_SOURCE_DIR}/finc/*.fin")
add_test(NAME "finfmt:check-selfhost"
    COMMAND $<TARGET_FILE:finfmt> --check ${FINFMT_SELFHOST_FILES})

file(GLOB FINFMT_SAMPLE_FILES
    "${CMAKE_SOURCE_DIR}/tests/samples/*.fin"
    "${CMAKE_SOURCE_DIR}/tests/samples/stdlib/*.fin")
add_test(NAME "finfmt:check-samples"
    COMMAND $<TARGET_FILE:finfmt> --check ${FINFMT_SAMPLE_FILES})

# Fixed ugly-input fixture. The ugly file must drift (WILL_FAIL: a clean
# --check would mean the fixture lost its teeth); the expected rendering must
# be --check clean, which is the idempotence half (fmt(fmt(x)) == fmt(x)).
add_test(NAME "finfmt:ugly-drifts"
    COMMAND $<TARGET_FILE:finfmt> --check "${CMAKE_CURRENT_SOURCE_DIR}/corpus/ugly.fin")
set_tests_properties(finfmt:ugly-drifts PROPERTIES WILL_FAIL TRUE)
add_test(NAME "finfmt:expected-clean"
    COMMAND $<TARGET_FILE:finfmt> --check "${CMAKE_CURRENT_SOURCE_DIR}/corpus/expected.fin")

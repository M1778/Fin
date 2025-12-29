# Locate GTest
find_package(GTest REQUIRED)

# Define the test executable
add_executable(fin_tests test_parser.cpp)

# Link against the core compiler library and GTest
target_link_libraries(fin_tests PRIVATE 
    fin_core 
    GTest::gtest 
    GTest::gtest_main
)

# Ensure tests can find the samples directory
# We copy the samples folder to the build directory for easy access
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/samples DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Register with CTest
include(GoogleTest)
gtest_discover_tests(fin_tests)

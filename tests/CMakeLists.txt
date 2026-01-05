find_package(GTest REQUIRED)

# Define the test executable here (and ONLY here)
add_executable(fin_tests test_parser.cpp)

target_link_libraries(fin_tests PRIVATE 
    fin_core 
    GTest::gtest 
    GTest::gtest_main
)

# Copy samples to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/samples DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

include(GoogleTest)
gtest_discover_tests(fin_tests)
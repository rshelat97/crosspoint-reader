# Gray16LutTest runs the fine-LUT config parser and builder, and the SD-card
# loader against the in-memory HalStorage/Logging stubs shared with test/cbz.
add_executable(Gray16LutTest
  Gray16LutTest.cpp
  ${REPO_ROOT}/src/util/Gray16Lut.cpp
)
target_include_directories(Gray16LutTest PRIVATE
  ${REPO_ROOT}/test/cbz/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(Gray16LutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Gray16LutTest)

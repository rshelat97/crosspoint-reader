add_executable(Gray16Test
  Gray16Test.cpp
  ${REPO_ROOT}/lib/Gray16/Gray16.cpp
)
target_include_directories(Gray16Test PRIVATE
  ${REPO_ROOT}/lib/Gray16
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(Gray16Test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Gray16Test)

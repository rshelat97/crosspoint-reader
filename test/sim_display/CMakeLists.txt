add_executable(SimDisplayTest SimDisplayTest.cpp ${REPO_ROOT}/simulator/sim/sim_display.cpp)
target_include_directories(SimDisplayTest PRIVATE ${REPO_ROOT}/simulator/sim)
target_link_libraries(SimDisplayTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SimDisplayTest)

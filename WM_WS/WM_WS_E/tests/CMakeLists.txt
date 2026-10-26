include(GoogleTest)

foreach(test_name IN ITEMS engine_error valve)
  add_executable(${test_name}_test ${test_name}_test.cpp)
  target_link_libraries(${test_name}_test PRIVATE engine_core GTest::gtest_main)
  gtest_discover_tests(${test_name}_test PROPERTIES TIMEOUT 10)
endforeach()

add_executable(poll_test poll_test.cpp)
target_compile_features(poll_test PRIVATE cxx_std_23)
target_include_directories(poll_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(poll_test PRIVATE GTest::gtest_main ninttp::ninttp)
gtest_discover_tests(poll_test PROPERTIES TIMEOUT 10)

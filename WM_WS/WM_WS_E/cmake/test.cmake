include_guard(DIRECTORY)

include(CTest)
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
  endif()
  set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5 # v1.16.0
  )
  FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

foreach(test_name IN ITEMS engine_error valve)
  add_executable(${test_name}_test tests/${test_name}_test.cpp)
  target_link_libraries(${test_name}_test PRIVATE engine_core GTest::gtest_main)
  gtest_discover_tests(${test_name}_test PROPERTIES TIMEOUT 10)
endforeach()

add_executable(poll_test tests/poll_test.cpp)
target_compile_features(poll_test PRIVATE cxx_std_23)
target_include_directories(poll_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(poll_test PRIVATE GTest::gtest_main ninttp::ninttp)
gtest_discover_tests(poll_test PROPERTIES TIMEOUT 10)

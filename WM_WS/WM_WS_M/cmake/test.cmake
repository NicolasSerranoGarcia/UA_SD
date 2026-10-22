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

add_executable(monitor_options_test tests/options_test.cpp src/app/options.cpp)
target_compile_features(monitor_options_test PRIVATE cxx_std_23)
target_include_directories(monitor_options_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared"
)
target_link_libraries(monitor_options_test PRIVATE
  GTest::gtest_main Boost::program_options ninttp::ninttp
)
gtest_discover_tests(monitor_options_test PROPERTIES TIMEOUT 10)

add_executable(monitor_connect_test tests/connect_test.cpp)
target_compile_features(monitor_connect_test PRIVATE cxx_std_23)
target_include_directories(monitor_connect_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
target_link_libraries(monitor_connect_test PRIVATE
  GTest::gtest_main Boost::headers ninttp::ninttp Threads::Threads
)
gtest_discover_tests(monitor_connect_test PROPERTIES TIMEOUT 10)

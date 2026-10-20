cmake_minimum_required(VERSION 3.16)
project(watering_station_integration_tests LANGUAGES CXX)

option(BUILD_TESTING "Build integration tests" ON)
if(NOT BUILD_TESTING)
  return()
endif()
include(CTest)

set(ENGINE_EXECUTABLE "" CACHE FILEPATH "Path to the built WM_WS_E executable")
set(MONITOR_EXECUTABLE "" CACHE FILEPATH "Path to the built WM_WS_M executable")
foreach(binary IN ITEMS ENGINE_EXECUTABLE MONITOR_EXECUTABLE)
  if(NOT EXISTS "${${binary}}" OR IS_DIRECTORY "${${binary}}")
    message(FATAL_ERROR "Set ${binary} to an existing watering station executable")
  endif()
  get_filename_component(${binary} "${${binary}}" ABSOLUTE)
  file(TO_CMAKE_PATH "${${binary}}" ${binary})
endforeach()
configure_file(paths.hpp.in paths.hpp @ONLY)

# Process v2 and Asio are header-only; std::filesystem avoids a Boost.Filesystem dependency.
find_package(Boost 1.83 REQUIRED)
find_package(Threads REQUIRED)
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

add_executable(process_test process_test.cpp process_support.cpp)
target_compile_features(process_test PRIVATE cxx_std_23)
target_compile_definitions(process_test PRIVATE BOOST_PROCESS_USE_STD_FS)
target_include_directories(process_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(process_test PRIVATE GTest::gtest_main Boost::headers Threads::Threads)
if(WIN32)
  target_link_libraries(process_test PRIVATE ws2_32 ntdll)
endif()
include(GoogleTest)
gtest_discover_tests(process_test PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)

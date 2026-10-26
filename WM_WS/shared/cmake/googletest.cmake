include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/probe.cmake")
wm_ws_probe_package(GTest TARGETS GTest::gtest GTest::gtest_main RESULT gtest_available)
if(gtest_available)
  find_package(GTest CONFIG REQUIRED)
else()
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

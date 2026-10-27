include_guard(DIRECTORY)

add_library(wm_ws_kafka INTERFACE)

include("${CMAKE_CURRENT_LIST_DIR}/../../shared/cmake/probe.cmake")
wm_ws_probe_package(RdKafka TARGETS RdKafka::rdkafka++ RESULT rdkafka_available)
if(rdkafka_available)
  find_package(RdKafka CONFIG REQUIRED)
  target_link_libraries(wm_ws_kafka INTERFACE RdKafka::rdkafka++)
  return()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(RDKAFKA QUIET IMPORTED_TARGET rdkafka++ rdkafka)
endif()
if(TARGET PkgConfig::RDKAFKA)
  target_link_libraries(wm_ws_kafka INTERFACE PkgConfig::RDKAFKA)
  return()
endif()

include(FetchContent)
message(STATUS "(WM_WS) librdkafka not found; fetching librdkafka 2.11.1")
set(RDKAFKA_BUILD_EXAMPLES OFF CACHE BOOL "Build librdkafka examples" FORCE)
set(RDKAFKA_BUILD_TESTS OFF CACHE BOOL "Build librdkafka tests" FORCE)
set(RDKAFKA_BUILD_STATIC ON CACHE BOOL "Build fallback librdkafka statically")
FetchContent_Declare(
  librdkafka
  GIT_REPOSITORY https://github.com/confluentinc/librdkafka.git
  GIT_TAG 69b1865efdc0118cd017760d038d34e52fb3f0d0 # v2.11.1
)
FetchContent_MakeAvailable(librdkafka)
target_link_libraries(wm_ws_kafka INTERFACE rdkafka++ rdkafka)

# Match the installed header layout used by the Engine's includes.
file(MAKE_DIRECTORY "${librdkafka_BINARY_DIR}/include/librdkafka")
configure_file("${librdkafka_SOURCE_DIR}/src-cpp/rdkafkacpp.h"
  "${librdkafka_BINARY_DIR}/include/librdkafka/rdkafkacpp.h" COPYONLY)
foreach(header IN ITEMS rdkafka.h rdkafka_mock.h)
  configure_file("${librdkafka_SOURCE_DIR}/src/${header}"
    "${librdkafka_BINARY_DIR}/include/librdkafka/${header}" COPYONLY)
endforeach()
target_include_directories(wm_ws_kafka INTERFACE "${librdkafka_BINARY_DIR}/include")

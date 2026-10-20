cmake_minimum_required(VERSION 3.16)

project(watering_station_engine)

include(cmake/boost.cmake)
include(cmake/kafka.cmake)
include(cmake/ninttp.cmake)

add_subdirectory(src/engine)

add_executable(WM_WS_E
  src/WM_WS_E.cpp
  src/app/app.cpp
  src/app/options.cpp
  src/app/health_server.cpp
)
target_link_libraries(WM_WS_E PRIVATE engine_core)
target_include_directories(WM_WS_E PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

#librdkafka++ dependency
target_compile_features(WM_WS_E PRIVATE cxx_std_23)
target_link_libraries(WM_WS_E PRIVATE PkgConfig::RDKAFKA)

target_link_libraries(WM_WS_E PRIVATE Boost::program_options ninttp::ninttp)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()

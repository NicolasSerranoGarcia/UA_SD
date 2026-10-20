cmake_minimum_required(VERSION 3.16)

project(watering_station_monitor)

include(cmake/boost.cmake)
include(cmake/ninttp.cmake)

add_executable(WM_WS_M
  src/WM_WS_M.cpp
  src/app/app.cpp
  src/app/options.cpp
  src/nap/reporter.cpp
)

target_compile_features(WM_WS_M PRIVATE cxx_std_23)

target_include_directories(WM_WS_M PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared"
)

target_link_libraries(WM_WS_M PRIVATE Boost::program_options ninttp::ninttp)

find_package(Threads REQUIRED)
target_link_libraries(WM_WS_M PRIVATE Threads::Threads)

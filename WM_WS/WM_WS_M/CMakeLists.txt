cmake_minimum_required(VERSION 3.16)

project(watering_station_monitor)

include(cmake/boost.cmake)
include(cmake/ninttp.cmake)

add_executable(WM_WS_M src/WM_WS_M.cpp)

target_compile_features(WM_WS_M PRIVATE cxx_std_23)

target_include_directories(WM_WS_M PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(WM_WS_M PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

#Boost.program_options dependency for command line parsing
target_link_libraries(WM_WS_M PRIVATE Boost::program_options)



target_link_libraries(WM_WS_M PRIVATE ninttp::ninttp)

find_package(Threads REQUIRED)
target_link_libraries(WM_WS_M PRIVATE Threads::Threads)

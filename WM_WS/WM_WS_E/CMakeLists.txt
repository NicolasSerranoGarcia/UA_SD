cmake_minimum_required(VERSION 3.16)

project(watering_station_engine)

include(cmake/boost.cmake)
include(cmake/kafka.cmake)
include(cmake/ninttp.cmake)

add_executable(WM_WS_E src/WM_WS_E.cpp)
target_include_directories(WM_WS_E PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/engine")
target_include_directories(WM_WS_E PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(WM_WS_E PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

#librdkafka++ dependency
target_compile_features(WM_WS_E PRIVATE cxx_std_23)
target_link_libraries(WM_WS_E PRIVATE PkgConfig::RDKAFKA)

#Boost.program_options dependency for command line parsing
target_link_libraries(WM_WS_E PRIVATE Boost::program_options)



target_link_libraries(WM_WS_E PRIVATE ninttp::ninttp)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

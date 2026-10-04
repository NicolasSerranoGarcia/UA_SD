add_executable(engine_multiple_deploys engine_multiple_deploys.cpp)

target_compile_features(engine_multiple_deploys PRIVATE cxx_std_23)
target_include_directories(engine_multiple_deploys PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include/engine")

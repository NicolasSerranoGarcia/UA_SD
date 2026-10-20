find_package(Threads REQUIRED)

add_library(engine_core engine.cpp)
target_compile_features(engine_core PUBLIC cxx_std_23)
target_include_directories(engine_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_link_libraries(engine_core PUBLIC Threads::Threads)

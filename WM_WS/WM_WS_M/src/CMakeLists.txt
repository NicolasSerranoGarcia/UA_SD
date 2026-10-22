add_library(wm_ws_monitor
  app/app.cpp
  app/options.cpp
  nap/reporter.cpp
)

target_compile_features(wm_ws_monitor PUBLIC cxx_std_23)

target_include_directories(wm_ws_monitor PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../shared"
)

target_link_libraries(wm_ws_monitor
  PUBLIC ninttp::ninttp Threads::Threads
  PRIVATE Boost::program_options
)

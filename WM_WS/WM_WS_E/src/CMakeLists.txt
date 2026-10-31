add_subdirectory(engine)

add_library(wm_ws_station kafka/station.cpp kafka/journal.cpp kafka/protocol.cpp)
target_link_libraries(wm_ws_station PUBLIC engine_core Boost::json ninttp::ninttp)

add_library(wm_ws_engine
  app/app.cpp
  app/options.cpp
  app/health_server.cpp
  app/terminal.cpp
  kafka/service.cpp
)

target_compile_features(wm_ws_engine PUBLIC cxx_std_23)

target_include_directories(wm_ws_engine
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../shared"
)

target_link_libraries(wm_ws_engine
  PUBLIC ninttp::ninttp
  PRIVATE engine_core wm_ws_station wm_ws_kafka Boost::program_options
)

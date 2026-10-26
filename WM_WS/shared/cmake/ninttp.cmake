include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/probe.cmake")
wm_ws_probe_package(ninttp VERSION 0.0.2 TARGETS ninttp::ninttp RESULT ninttp_available)
if(ninttp_available)
  find_package(ninttp 0.0.2 CONFIG REQUIRED)
else()
  include(FetchContent)

  message(STATUS "(WM_WS) ninttp not found; fetching the pinned revision")
  FetchContent_Declare(
    ninttp
    GIT_REPOSITORY https://github.com/NicolasSerranoGarcia/ninttp.git
    GIT_TAG 61e798539367f640f304825012b9c45066532d90 # IPv4 validation and parsing
  )
  FetchContent_MakeAvailable(ninttp)
endif()

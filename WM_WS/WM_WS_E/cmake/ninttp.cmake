include_guard(DIRECTORY)

include(FetchContent)

FetchContent_Declare(
  ninttp
  GIT_REPOSITORY https://github.com/NicolasSerranoGarcia/ninttp.git
  GIT_TAG 61e798539367f640f304825012b9c45066532d90 # IPv4 validation and parsing
)

FetchContent_MakeAvailable(ninttp)

cmake_minimum_required(VERSION 3.20.0)

project(HLE_AUDIO)

Include(FetchContent)

enable_testing()

option(HLEA_BUILD_EDITOR "Enable the build of editor app." ON)
option(HLEA_BUILD_TOOL "Enable the build of cli app to compile bank binary." ON)

#
# Dependecies
#

# flatbuffers
FetchContent_Declare(
  flatbuffers
  GIT_REPOSITORY https://github.com/google/flatbuffers.git
  GIT_TAG        06c5c7ed0bd987a918cf88caafb094f22cdd1721 #v2.0.8
)

set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(flatbuffers)

#
# Source modules
#

add_subdirectory(fbs_types)

# data layer
add_subdirectory(data_layer)

# runtime library
add_subdirectory(runtime)

if (HLEA_BUILD_EDITOR)
  add_subdirectory(editor)
endif()
if (HLEA_BUILD_TOOL)
  add_subdirectory(tool)
endif()

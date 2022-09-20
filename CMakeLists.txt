cmake_minimum_required(VERSION 3.20.0)

project(HLE_AUDIO)

Include(FetchContent)

enable_testing()

option(HLEA_BUILD_EDITOR "Enable the build of editor app." ON)
option(HLEA_BUILD_TOOL "Enable the build of cli app to compile bank binary." ON)
set(HLEA_FLATC_BUILD_DIR 
    ${CMAKE_CURRENT_SOURCE_DIR}/build_aux CACHE STRING
    "Flatbuffers release binary path to build into and run flatc exe from")

#
# Source modules
#

add_subdirectory(fbs_types)

# runtime library
add_subdirectory(runtime)

# data layer
if (HLEA_BUILD_EDITOR OR HLEA_BUILD_TOOL)
  add_subdirectory(data_layer)
endif()

if (HLEA_BUILD_EDITOR)
  add_subdirectory(editor)
endif()
if (HLEA_BUILD_TOOL)
  add_subdirectory(tool)
endif()

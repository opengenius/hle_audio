cmake_minimum_required(VERSION 3.20.0)

project(HLE_AUDIO)

Include(FetchContent)

enable_testing()

option(HLEA_BUILD_EDITOR "Enable the build of editor app." ON)
option(HLEA_BUILD_TOOL "Enable the build of cli app to compile bank binary." ON)

#
# Source modules
#

add_library(runtime_data_types INTERFACE)
target_include_directories(runtime_data_types INTERFACE rt_types)

add_library(common_private INTERFACE)
target_include_directories(common_private INTERFACE common/include)


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

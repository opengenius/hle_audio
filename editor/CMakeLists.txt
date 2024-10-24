Include(FetchContent)

#
# Dependecies
#

# glfw
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(glfw)

# imgui
add_subdirectory(libs/imgui)

#
# Source modules
#

# domain layer
add_subdirectory(logic)

add_executable(hlea_editor WIN32
    src/main.cpp
    src/app_state.cpp
    src/app_view.cpp
    src/nodes_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editor.rc # win resource file
)

if (MSVC)
    # Tell MSVC to use main instead of WinMain for Windows subsystem executables
    set_target_properties(hlea_editor PROPERTIES
                            LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

target_link_libraries(hlea_editor 
    glfw imgui
    hlea_editor_logic
    hlea_runtime_editor
    hlea_tool_rt
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/res DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

cmake_minimum_required(VERSION 3.14.0)

project(imgui)

#
# Dependecies
#

add_library(${PROJECT_NAME} STATIC
    src/imgui.cpp
    src/imgui_draw.cpp
    src/imgui_widgets.cpp
    src/imgui_tables.cpp
    src/imgui_demo.cpp
    src/backends/imgui_impl_glfw.cpp
    src/backends/imgui_impl_opengl3.cpp

    src/imgui_etx.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

target_link_libraries(${PROJECT_NAME} glfw)

target_include_directories(${PROJECT_NAME} PUBLIC 
    include
)
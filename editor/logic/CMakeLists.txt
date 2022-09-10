add_library(hlea_editor_logic STATIC
    src/commands.cpp
    src/cmd_stack.cpp
    src/app_logic.cpp
)

set_property(TARGET hlea_editor_logic PROPERTY CXX_STANDARD 17)

target_include_directories(hlea_editor_logic 
    PUBLIC 
        include
    PRIVATE 
        ${CMAKE_CURRENT_BINARY_DIR}
        ../libs
)

target_link_libraries(hlea_editor_logic
    hlea_data_layer
)

# tests
enable_testing()
add_subdirectory(tests)
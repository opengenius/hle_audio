add_library(hlea_editor_logic STATIC
    src/commands.cpp
    src/cmd_stack.cpp
    src/app_logic.cpp
)

target_compile_features(hlea_editor_logic PUBLIC cxx_std_20)

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
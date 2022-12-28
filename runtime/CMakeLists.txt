add_library(hlea_runtime STATIC
    src/runtime.cpp
    src/node_state_stack.cpp
    src/default_allocator.cpp
    src/ma_impl.c
    src/file_api_vfs_bridge.cpp
)

target_include_directories(hlea_runtime
    PUBLIC 
        include
)

target_link_libraries(hlea_runtime
    PRIVATE
        runtime_data_types
        common_private
)

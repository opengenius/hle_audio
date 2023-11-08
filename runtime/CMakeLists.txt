FetchContent_Declare(minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG        afb604c06bc8beb145fecd42c0ceb5bda8795144
    GIT_SHALLOW    ON
)
FetchContent_Populate(minimp3)

set(RUNTIME_SRC
    src/node_state_stack.cpp
    src/default_allocator.cpp
    src/file_api_vfs_bridge.cpp
    src/decoder_mp3.cpp
    src/async_file_reader.cpp
    src/push_decoder_data_source.cpp
    src/streaming_data_source.cpp
    src/chunk_streaming_cache.cpp
)

add_library(hlea_runtime_common STATIC
    ${RUNTIME_SRC}
)

target_include_directories(hlea_runtime_common
    PUBLIC 
        include
    PRIVATE
        ${minimp3_SOURCE_DIR}
)

target_link_libraries(hlea_runtime_common
    PRIVATE
        common_private
    PUBLIC
        runtime_data_types
        hlea_rt_libs
)

add_library(hlea_runtime STATIC
    src/runtime.cpp
)

# duplicate public includes from private hlea_runtime_common
target_include_directories(hlea_runtime
    PUBLIC 
        include
)

target_link_libraries(hlea_runtime
    PRIVATE
        hlea_runtime_common
)

if (HLEA_BUILD_EDITOR)
    add_library(hlea_runtime_editor STATIC
        src/runtime.cpp
        src/editor_runtime.cpp
    )

    target_compile_definitions(hlea_runtime_editor 
        PRIVATE 
            HLEA_USE_RT_EDITOR
    )

    # duplicate public includes from private hlea_runtime_common    
    target_include_directories(hlea_runtime_editor
        PUBLIC 
            include
    )

    target_link_libraries(hlea_runtime_editor
        PRIVATE
            hlea_runtime_common
    )
endif()
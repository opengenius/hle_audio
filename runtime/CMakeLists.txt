FetchContent_Declare(minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG        afb604c06bc8beb145fecd42c0ceb5bda8795144
    GIT_SHALLOW    ON
)
FetchContent_Populate(minimp3)

add_library(hlea_runtime STATIC
    src/default_allocator.cpp
    src/file_api_vfs_bridge.cpp
    src/decoder_mp3.cpp
    src/decoder_pcm.cpp
    src/async_file_reader.cpp
    src/push_decoder_data_source.cpp
    src/streaming_data_source.cpp
    src/chunk_streaming_cache.cpp
    src/buffer_data_source.cpp
    src/runtime.cpp
    src/runtime_groups.cpp
)

target_link_libraries(hlea_runtime
PRIVATE
    common_private
    hlea_rt_libs
PUBLIC
    runtime_data_types
)

target_include_directories(hlea_runtime
PUBLIC 
    include
PRIVATE
    ${minimp3_SOURCE_DIR}
)

if (HLEA_BUILD_EDITOR)
    add_library(hlea_runtime_editor STATIC
        src/editor_runtime.cpp
    )

    target_link_libraries(hlea_runtime_editor
    PUBLIC
        hlea_runtime
    PRIVATE
        hlea_rt_libs
    )
endif()
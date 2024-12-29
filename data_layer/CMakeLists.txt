# rapidjson
FetchContent_Declare(rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG        f54b0e47a08782a6131cc3d60f94d038fa6e0a51 # v1.1.0
    GIT_SHALLOW    ON
    SOURCE_SUBDIR  fake_dir_name
)
FetchContent_MakeAvailable(rapidjson)

# cmake_minimum_required(VERSION 3.24.0)
# FetchContent_Declare(Ogg
#     GIT_REPOSITORY https://github.com/xiph/ogg.git
#     GIT_TAG        e1774cd77f471443541596e09078e78fdc342e4f # v1.3.5
#     GIT_SHALLOW    ON
#     OVERRIDE_FIND_PACKAGE
# )
# FetchContent_Declare(vorbis
#     GIT_REPOSITORY https://github.com/xiph/vorbis.git
#     GIT_TAG        0657aee69dec8508a0011f47f3b69d7538e9d262 # v1.3.7
#     GIT_SHALLOW    ON
# )
# FetchContent_MakeAvailable(vorbis)

add_library(hlea_data_layer STATIC
    src/index_id.cpp
    src/data_state.cpp
    src/data_state_json.cpp
    src/data_state_rt_blob.cpp
    src/data_state_v1.cpp
    src/data_state_v2.cpp
)

target_compile_features(hlea_data_layer PUBLIC cxx_std_20)

target_include_directories(hlea_data_layer 
    PUBLIC 
        include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${rapidjson_SOURCE_DIR}/include
)

target_link_libraries(hlea_data_layer
    PUBLIC
        runtime_data_types
    PRIVATE
        common_private
)

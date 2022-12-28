# rapidjson
FetchContent_Declare(rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG        f54b0e47a08782a6131cc3d60f94d038fa6e0a51 # v1.1.0
    GIT_SHALLOW    ON
)
FetchContent_Populate(rapidjson)

add_library(hlea_data_layer STATIC
    src/index_id.cpp
    src/data_state.cpp
    src/data_state_json.cpp
    src/data_state_rt_blob.cpp
)

target_compile_features(hlea_data_layer PUBLIC cxx_std_17)

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

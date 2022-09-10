
flatbuffers_generate_headers(TARGET generated_data_types
    SCHEMAS sound_data_types.fbs
    FLAGS --gen-object-api
)

set(generated_target_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_data_types")
set(generated_include "${generated_target_dir}/sound_data_types_generated.h")

add_custom_target(generated_data_types_generate_includes ALL DEPENDS ${generated_include})

add_library(generated_data_types_internal INTERFACE)
target_link_libraries(generated_data_types_internal 
    INTERFACE 
        flatbuffers 
        generated_data_types
)

add_dependencies(generated_data_types_internal generated_data_types_generate_includes)

add_library(HLEA::generated_data_types ALIAS generated_data_types_internal)

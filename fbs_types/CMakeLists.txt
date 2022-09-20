#
# Dependecies
#

# flatbuffers
if (NOT TARGET flatbuffers)
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
    if (flatbuffers_SOURCE_DIR)    
        add_subdirectory(${flatbuffers_SOURCE_DIR} ${CMAKE_BINARY_DIR}/flatbuffers)
    else()
        FetchContent_Declare(
            flatbuffers
            GIT_REPOSITORY https://github.com/google/flatbuffers.git
            GIT_TAG        06c5c7ed0bd987a918cf88caafb094f22cdd1721 #v2.0.8
        )
        FetchContent_MakeAvailable(flatbuffers)
    endif()
endif()

if (NOT FLATBUFFERS_FLATC_EXECUTABLE)
    # build flatc host release cli
    set(FLATC_WD ${HLEA_FLATC_BUILD_DIR})
    file(MAKE_DIRECTORY ${FLATC_WD})
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -G Ninja 
            -DCMAKE_BUILD_TYPE=Release
            -DFLATBUFFERS_BUILD_TESTS=OFF
            -DFLATBUFFERS_BUILD_FLATHASH=OFF
            ${flatbuffers_SOURCE_DIR}
        WORKING_DIRECTORY ${FLATC_WD}
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build . --target flatc
        WORKING_DIRECTORY ${FLATC_WD}
    )
    set(FLATBUFFERS_FLATC_EXECUTABLE ${FLATC_WD}/flatc)
endif()

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

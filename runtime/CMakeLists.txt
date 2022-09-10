add_library(hlea_runtime STATIC
    src/hlea_runtime.cpp
)

set_property(TARGET hlea_runtime PROPERTY CXX_STANDARD 17)

target_include_directories(hlea_runtime
    PUBLIC 
        include
)

target_link_libraries(hlea_runtime
    PRIVATE
        HLEA::generated_data_types
)

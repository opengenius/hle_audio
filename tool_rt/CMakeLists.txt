add_library(hlea_tool_rt STATIC
    file_data_provider.cpp
)

target_include_directories(hlea_tool_rt
    PUBLIC .   
)

target_link_libraries(hlea_tool_rt 
    hlea_data_layer
    hlea_rt_libs
)

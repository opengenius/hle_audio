add_library(hlea_rt_libs STATIC
    ma_impl.c
)

target_include_directories(hlea_rt_libs
    PUBLIC 
        .       
)

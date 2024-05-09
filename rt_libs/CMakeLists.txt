if (APPLE)
    set(SOURCES ma_impl.m)
else()
    set(SOURCES ma_impl.c)
endif()

add_library(hlea_rt_libs STATIC ${SOURCES})

target_include_directories(hlea_rt_libs
    PUBLIC 
        .       
)

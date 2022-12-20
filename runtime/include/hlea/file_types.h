#pragma once

#include <cstdint>

enum hlea_file_handle_t : intptr_t;

struct hlea_file_ti {
    hlea_file_handle_t (*open)(void* sys, const char* file_path);
    void (*close)(void* sys, hlea_file_handle_t file);

    size_t (*size)(void* sys, hlea_file_handle_t file);

    size_t (*read)(void* sys, hlea_file_handle_t file, void* dst, size_t dst_size);

    size_t (*tell)(void* sys, hlea_file_handle_t file);
    void (*seek)(void* sys, hlea_file_handle_t file, size_t pos);
};

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

/*
enum hlea_async_file_handle_t : uint32_t;
const hlea_async_file_handle_t hlea_invalid_async_file_handle = {};

struct hlea_async_read_request_t {
    hlea_async_file_handle_t file;
    uint32_t offset;
    uint8_t* out_buffer_data;
    size_t out_buffer_size;
};

struct hlea_async_file_ti {
    void (*request_read)(void* sys, const hlea_async_read_request_t& read_request);
    void (*check_request_running)(void* sys, int handle);
};
*/

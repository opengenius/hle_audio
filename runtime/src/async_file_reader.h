#pragma once

#include <cstdint>
#include "internal_alloc_types.h"
#include "hlea/file_types.h"
#include "rt_types.h"

// forward declare ma_vfs types
typedef void* ma_handle;
typedef void      ma_vfs;
typedef ma_handle ma_vfs_file;

namespace hle_audio {
namespace rt {

enum async_file_handle_t : uint32_t;
const async_file_handle_t invalid_async_file_handle = {};

struct async_file_reader_create_info_t {
    allocator_t allocator;
    ma_vfs* vfs;
};

struct async_file_reader_t;

async_file_reader_t* create_async_file_reader(const async_file_reader_create_info_t& info);
void destroy(async_file_reader_t* reader);

async_file_handle_t start_async_reading(async_file_reader_t* reader, ma_vfs_file f);
void stop_async_reading(async_file_reader_t* reader, async_file_handle_t afile);

enum async_read_token_t : uint32_t;

struct async_read_request_t {
    async_file_handle_t file;
    uint32_t offset;
    data_buffer_t out_buffer;
};

async_read_token_t request_read(async_file_reader_t* reader, const async_read_request_t& request);
bool check_request_running(async_file_reader_t* reader, async_read_token_t token);

}
}

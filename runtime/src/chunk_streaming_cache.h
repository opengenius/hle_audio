#pragma once

#include <cstdint>
#include "rt_types.h"
#include "async_file_reader.h"

namespace hle_audio {
namespace rt {

struct chunk_streaming_cache_t;

/*
 * opened file handle
*/ 
enum streaming_source_handle : uint32_t;

struct chunk_request_t {
    streaming_source_handle src;
    range_t buffer_block;
    uint32_t block_offset;
};

enum class chunk_status_e {
    READING = 0,
    READY
};

struct chunk_request_result_t {
    uint32_t index;
    chunk_status_e status;
    data_buffer_t data;
};

struct chunk_streaming_cache_init_info_t {
    allocator_t allocator;
    async_file_reader_t* async_io;
};

chunk_streaming_cache_t* create_cache(const chunk_streaming_cache_init_info_t& info);
void destroy(chunk_streaming_cache_t* cache);

streaming_source_handle register_source(chunk_streaming_cache_t* cache, async_file_handle_t file);
void deregister_source(chunk_streaming_cache_t* cache, streaming_source_handle src);

chunk_request_result_t acquire_chunk(chunk_streaming_cache_t& cache, const chunk_request_t& request);
void release_chunk(chunk_streaming_cache_t& cache, uint32_t chunk_index);
chunk_status_e chunk_status(const chunk_streaming_cache_t& cache, uint32_t chunk_index);

}
}

#pragma once

#include "chunk_streaming_cache.h"
#include "rt_types.h"

struct ma_engine;

struct bank_streaming_source_info_t {
    using streaming_source_handle = hle_audio::rt::streaming_source_handle;
    using range_t = hle_audio::rt::range_t;
    
    streaming_source_handle streaming_src;
    range_t file_range;
};

namespace hle_audio {
namespace rt {

struct editor_runtime_t;

struct runtime_env_t {
    ma_vfs* pVFS;
    allocator_t allocator;
    async_file_reader_t* async_io;
    chunk_streaming_cache_t* cache;
    ma_engine* engine;
};
void bind(editor_runtime_t* editor_rt, const runtime_env_t* env);

bank_streaming_source_info_t retrieve_streaming_info(editor_runtime_t* editor_rt, uint32_t file_index);

}
}

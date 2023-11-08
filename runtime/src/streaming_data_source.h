#pragma once

#include "miniaudio_public.h"
#include "push_decoder_data_source.h"
#include "internal_alloc_types.h"
#include "internal_jobs_types.h"

struct hlea_context_t;

namespace hle_audio {
namespace rt {

struct mp3_decoder_t;

struct streaming_data_source_t {
    ma_data_source_base base;

    // todo: make abstact (mp3|wav|ogg|etc)
    mp3_decoder_t* decoder;
    push_decoder_data_source_t decoder_reader;

    ma_uint64 length_in_samples;
    ma_uint32 channels;
    ma_uint32 sample_rate;

    ma_uint64 read_cursor;
};


struct streaming_data_source_init_info_t {
    allocator_t allocator;
    chunk_streaming_cache_t* streaming_cache;
    jobs_t jobs;

    const ma_decoder_config* pConfig;
    streaming_source_handle input_src;
    range_t file_range;
    file_data_t::meta_t meta;
};

ma_result streaming_data_source_init(streaming_data_source_t* pDataSource, const streaming_data_source_init_info_t& info);
void streaming_data_source_uninit(streaming_data_source_t* pDataSource);

bool is_ready_to_deinit(streaming_data_source_t* pDataSource);

}
}

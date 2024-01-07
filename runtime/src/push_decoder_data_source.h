#pragma once

#include "rt_types.h"
#include "chunk_streaming_cache.h"
#include "decoder.h"

namespace hle_audio {
namespace rt {

struct input_chunk_t {
    uint32_t chunk_id;
};

static const size_t MAX_DS_INPUTS = 2;//MAX_INPUT_BUFFERS;

struct push_decoder_data_source_t {
    chunk_streaming_cache_t* streaming_cache;

    decoder_t decoder;
    streaming_source_handle input_src;
    range_t buffer_block;

    // inputs
    input_chunk_t inputs[MAX_DS_INPUTS];
    uint8_t input_count;

    // pending input
    uint32_t input_block_offset;
    data_buffer_t chunk_buffer;
    async_read_token_t read_token;

    // outpus
    data_buffer_t read_buffer;
    uint64_t read_bytes;
};

struct push_decoder_data_source_init_info_t {
    chunk_streaming_cache_t* streaming_cache;
    streaming_source_handle input_src;
    range_t buffer_block;
    decoder_t decoder;
};

void init(push_decoder_data_source_t& src, const push_decoder_data_source_init_info_t& iinfo);
void deinit(push_decoder_data_source_t& src);

bool read_decoded(push_decoder_data_source_t& src, uint8_t channels, void* frame_out, uint64_t frame_count, uint64_t* frames_read);

}
}

#include "push_decoder_data_source.h"

#include <cstring>
#include <algorithm>

namespace hle_audio {
namespace rt {

static bool prepare_next_chunk(push_decoder_data_source_t& src);

void init(push_decoder_data_source_t& src, const push_decoder_data_source_init_info_t& iinfo) {
    src = {};

    src.streaming_cache = iinfo.streaming_cache;
    src.input_src = iinfo.input_src;
    src.buffer_block = iinfo.buffer_block;
    src.decoder = iinfo.decoder;

    // prepare first chunk
    prepare_next_chunk(src);
}

void deinit(push_decoder_data_source_t& src) {
    assert(!is_running(src.decoder) && "decoder should have released its inputs");

    for (size_t i = 0; i < src.input_count; ++i) {
        release_chunk(*src.streaming_cache, src.inputs[i].chunk_id);
    }
    src.input_count = 0;
}

/**
 * @brief 
 * 
 * @param src 
 * @return true if has more chunks
 * @return false when last is reached
 */
static bool prepare_next_chunk(push_decoder_data_source_t& src) {
    // got enough inputs already
    if (src.input_count == MAX_DS_INPUTS) return true;

    // check if reached the last chunk
    if (src.input_block_offset == src.buffer_block.size) {
        return false;
    }

    chunk_request_t req = {};
    req.src = src.input_src;
    req.buffer_block = src.buffer_block;
    req.block_offset = src.input_block_offset;
    auto ch_res = acquire_chunk(*src.streaming_cache, req);

    // no chunks avaliable
    if (ch_res.index == ~0u) return true;

    // prepare next input
    input_chunk_t next_input = {};
    next_input.chunk_id = ch_res.index;

    src.inputs[src.input_count++] = next_input;
    src.chunk_buffer = ch_res.data;

    return true;
}

/**
 * @brief 
 * 
 * @param src 
 * @param channels 
 * @param frame_out output frame array (frame_count size)
 * @param frame_count number of frames to read
 * @param frames_read out
 * @return true when read successfully (frames_read is 0 when source end is reached)
 * @return false if there is still some data to read, but no data ready (data starvation case)
 */
bool read_decoded(push_decoder_data_source_t& src, uint8_t channels, uint8_t sample_byte_size, 
        void* frame_out, uint64_t frame_count, uint64_t* frames_read) {
    // deque ready output
    auto processed_inputs_count = release_consumed_inputs(src.decoder);
    if (processed_inputs_count) {
        // release chunks
        for (size_t i = 0; i < processed_inputs_count; ++i) {
            release_chunk(*src.streaming_cache, src.inputs[i].chunk_id);
        }
        for (size_t i = processed_inputs_count; i < src.input_count; ++i) {
            src.inputs[i - processed_inputs_count] = src.inputs[i];
        }
        src.input_count -= uint8_t(processed_inputs_count);
    }

    // finished reading file chunk
    if (!is_empty(src.chunk_buffer) && 
            src.input_count && 
            chunk_status(*src.streaming_cache, src.inputs[src.input_count - 1].chunk_id) == chunk_status_e::READY) {
        // queue ready to decode buffer
        bool last_chunk = src.input_block_offset + src.chunk_buffer.size == src.buffer_block.size;
        queue_input(src.decoder, src.chunk_buffer, last_chunk);

        src.input_block_offset += src.chunk_buffer.size;
        src.chunk_buffer = {};
    }

    bool has_more_inputs = src.input_count > 0;

    // request next chunk
    if (is_empty(src.chunk_buffer)) {
        bool has_more_chunks = prepare_next_chunk(src);
        has_more_inputs |= has_more_chunks;
    }

    // acquire ready output buffer
    if (src.read_buffer.size == src.read_bytes) {
        src.read_bytes = 0;
        src.read_buffer = next_output(src.decoder, src.read_buffer);
    }

    if (is_empty(src.read_buffer)) {
        if (!has_more_inputs) return true; // no more data
        return false;
    }

    uint64_t frames_in_bytes = frame_count * sample_byte_size * channels;

    uint64_t bytes_consumed = std::min(src.read_buffer.size - src.read_bytes, frames_in_bytes);
    memcpy(frame_out, (uint8_t*)src.read_buffer.data + src.read_bytes, size_t(bytes_consumed));
    src.read_bytes += bytes_consumed;

    // request next read buffer
    if (src.read_buffer.size == src.read_bytes) {
        src.read_bytes = 0;
        src.read_buffer = next_output(src.decoder, src.read_buffer);
    }

    *frames_read = bytes_consumed / (sample_byte_size * channels);

    return true;
}

}
}

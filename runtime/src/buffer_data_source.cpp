#include "buffer_data_source.h"

#include <algorithm>
#include "data_source_utils.inl"

namespace hle_audio {
namespace rt {

static ma_result buffer_data_source_read(ma_data_source* data_source, void* frames_out, ma_uint64 frame_count, ma_uint64* frames_read) {
    buffer_data_source_t* src = (buffer_data_source_t*)data_source;

    uint8_t channels = src->meta.channels;
    const auto sample_byte_size = get_sample_byte_size(src->format);

    // acquire ready output buffer
    if (is_empty(src->read_buffer) || (src->read_buffer.size == src->read_bytes)) {
        src->read_bytes = 0;
        src->read_buffer = next_output(src->decoder, src->read_buffer);
    }

    if (is_empty(src->read_buffer)) {
        if (src->read_cursor == src->meta.length_in_samples) return MA_SUCCESS;
        // still has something to read, but no buffer ready
        else return MA_BUSY;
    }

    if (0 < src->skip_read_bytes) {
        auto skipped_read_bytes = std::min(src->skip_read_bytes, static_cast<uint64_t>(src->read_buffer.size));
        src->read_bytes += skipped_read_bytes;
        src->skip_read_bytes -= skipped_read_bytes;
        if (src->read_buffer.size == src->read_bytes) return MA_BUSY;
    }

    uint64_t frames_in_bytes = frame_count * sample_byte_size * channels;

    uint64_t bytes_consumed = std::min(src->read_buffer.size - src->read_bytes, frames_in_bytes);
    memcpy(frames_out, (uint8_t*)src->read_buffer.data + src->read_bytes, size_t(bytes_consumed));
    src->read_bytes += bytes_consumed;

    // request next read buffer
    if (src->read_buffer.size == src->read_bytes) {
        src->read_bytes = 0;
        src->read_buffer = next_output(src->decoder, src->read_buffer);
    }

    *frames_read = bytes_consumed / (sample_byte_size * channels);

    src->read_cursor += *frames_read;

    assert(src->read_cursor <= src->meta.length_in_samples);

    return MA_SUCCESS;
}

static ma_result buffer_data_source_seek(ma_data_source* data_source, ma_uint64 frameIndex) {
    buffer_data_source_t* ds = (buffer_data_source_t*)data_source;

    uint8_t channels = ds->meta.channels;
    const auto sample_byte_size = get_sample_byte_size(ds->format);

    flush(ds->decoder);
    queue_input(ds->decoder, ds->buffer, true);
    ds->read_buffer = {};
    ds->read_bytes = 0;
    ds->read_cursor = frameIndex;
    ds->skip_read_bytes = frameIndex * sample_byte_size * channels;
    
    return MA_SUCCESS;
}

static ma_result buffer_data_source_get_data_format(
        ma_data_source* data_source, 
        ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap) {
    buffer_data_source_t* ds = (buffer_data_source_t*)data_source;

    if (pFormat)
        *pFormat = ds->format;

    if (pChannels)
        *pChannels = ds->meta.channels;

    if (pSampleRate)
        *pSampleRate = ds->meta.sample_rate;
    
    return MA_SUCCESS;
}

static ma_result buffer_data_source_get_cursor(ma_data_source* data_source, ma_uint64* pCursor) {
    buffer_data_source_t* streaming_ds = (buffer_data_source_t*)data_source;

    *pCursor = streaming_ds->read_cursor;

    return MA_SUCCESS;
}

static ma_result buffer_data_source_get_length(ma_data_source* data_source, ma_uint64* pLength) {
    buffer_data_source_t* ds = (buffer_data_source_t*)data_source;

    assert(ds->meta.length_in_samples);
    *pLength = ds->meta.length_in_samples;

    return MA_SUCCESS;
}

static ma_data_source_vtable g_buffer_data_source_vtable = {
    buffer_data_source_read,
    buffer_data_source_seek,
    buffer_data_source_get_data_format,
    buffer_data_source_get_cursor,
    buffer_data_source_get_length
};

ma_result buffer_data_source_init(buffer_data_source_t* data_source, const buffer_data_source_init_info_t& info) {
    ma_data_source_config baseConfig;

    baseConfig = ma_data_source_config_init();
    baseConfig.vtable = &g_buffer_data_source_vtable;

    *data_source = {};
    ma_result result = ma_data_source_init(&baseConfig, &data_source->base);
    if (result != MA_SUCCESS) {
        return result;
    }

    data_source->decoder = info.decoder;
    data_source->format = info.format;
    data_source->meta = info.meta;
    data_source->buffer = info.buffer;

    queue_input(data_source->decoder, data_source->buffer, true);

    return MA_SUCCESS;
}

void buffer_data_source_uninit(buffer_data_source_t* data_source) {
    assert(!is_running(data_source->decoder) && "decoder should have released its inputs");

    // uninitialize the base data source.
    ma_data_source_uninit(&data_source->base);
}

}} // hle_audio::rt

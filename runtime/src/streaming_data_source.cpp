#include "streaming_data_source.h"
#include "decoder_mp3.h"

namespace hle_audio {
namespace rt {

static ma_result streaming_data_source_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
    streaming_data_source_t* streaming_ds = (streaming_data_source_t*)pDataSource;

    auto res = read_decoded(streaming_ds->decoder_reader, streaming_ds->channels, pFramesOut, frameCount, pFramesRead);
    if (!res) {
        // assert(streaming_ds->read_cursor == 0); // todo: handle starvation?
        return MA_BUSY;
    }

    streaming_ds->read_cursor += *pFramesRead;

    return MA_SUCCESS;
}

static ma_result streaming_data_source_seek(ma_data_source* pDataSource, ma_uint64 frameIndex) {
    assert(false);
    
    // Seek to a specific PCM frame here. Return MA_NOT_IMPLEMENTED if seeking is not supported.
    return MA_NOT_IMPLEMENTED;

    // ma_resource_manager_data_stream_seek_to_pcm_frame
}

static ma_result streaming_data_source_get_data_format(
        ma_data_source* pDataSource, 
        ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap) {
    streaming_data_source_t* streaming_ds = (streaming_data_source_t*)pDataSource;

    if (pFormat)
        *pFormat = ma_format_f32;

    if (pChannels)
        *pChannels = streaming_ds->channels;

    if (pSampleRate)
        *pSampleRate = streaming_ds->sample_rate;
    
    return MA_SUCCESS;
}

static ma_result streaming_data_source_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor) {
    streaming_data_source_t* streaming_ds = (streaming_data_source_t*)pDataSource;

    *pCursor = streaming_ds->read_cursor;
    return MA_SUCCESS;
}

static ma_result streaming_data_source_get_length(ma_data_source* pDataSource, ma_uint64* pLength) {
    streaming_data_source_t* streaming_ds = (streaming_data_source_t*)pDataSource;

    assert(streaming_ds->length_in_samples);

    *pLength = streaming_ds->length_in_samples;
    return MA_SUCCESS;
}

static ma_data_source_vtable g_streaming_data_source_vtable = {
    streaming_data_source_read,
    streaming_data_source_seek,
    streaming_data_source_get_data_format,
    streaming_data_source_get_cursor,
    streaming_data_source_get_length
};

ma_result streaming_data_source_init(streaming_data_source_t* pDataSource, const streaming_data_source_init_info_t& info) {
    ma_data_source_config baseConfig;

    baseConfig = ma_data_source_config_init();
    baseConfig.vtable = &g_streaming_data_source_vtable;

    *pDataSource = {};
    ma_result result = ma_data_source_init(&baseConfig, &pDataSource->base);
    if (result != MA_SUCCESS) {
        return result;
    }

    // get decoder
    mp3_decoder_create_info_t dec_init_info = {};
    dec_init_info.allocator = info.allocator;
    dec_init_info.jobs = info.jobs;
    pDataSource->decoder = create_decoder(dec_init_info);

    // init decoder reader
    push_decoder_data_source_init_info_t push_dec_info = {};
    push_dec_info.streaming_cache = info.streaming_cache;
    push_dec_info.input_src = info.input_src;
    push_dec_info.buffer_block = info.file_range;
    push_dec_info.decoder = cast_to_decoder(pDataSource->decoder);
    init(pDataSource->decoder_reader, push_dec_info);

    pDataSource->length_in_samples = info.meta.length_in_samples;
    pDataSource->channels = info.meta.channels;
    pDataSource->sample_rate = info.meta.sample_rate;

    return MA_SUCCESS;
}

void streaming_data_source_uninit(streaming_data_source_t* pDataSource) {
    assert(is_ready_to_deinit(pDataSource));
    
    deinit(pDataSource->decoder_reader);
    
    destroy(pDataSource->decoder);
    pDataSource->decoder = nullptr;

    // uninitialize the base data source.
    ma_data_source_uninit(&pDataSource->base);
}

bool is_ready_to_deinit(streaming_data_source_t* pDataSource) {
    if (is_running(pDataSource->decoder))
        return false;

    return true;
}

}
}

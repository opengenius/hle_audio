#pragma once

#include "miniaudio_public.h"
#include "push_decoder_data_source.h"

namespace hle_audio {
namespace rt {

struct streaming_data_source_t {
    ma_data_source_base base;

    push_decoder_data_source_t decoder_reader;

    ma_format format;
    ma_uint64 length_in_samples;
    ma_uint32 channels;
    ma_uint32 sample_rate;

    ma_uint64 read_cursor;
};


struct streaming_data_source_init_info_t {
    ma_format format;
    file_data_t::meta_t meta;

    push_decoder_data_source_init_info_t decoder_reader_info;
};

ma_result streaming_data_source_init(streaming_data_source_t* pDataSource, const streaming_data_source_init_info_t& info);
void streaming_data_source_uninit(streaming_data_source_t* pDataSource);

}
}

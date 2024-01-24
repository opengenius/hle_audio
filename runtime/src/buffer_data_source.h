#pragma once

#include "miniaudio_public.h"
#include "rt_types.h"
#include "decoder.h"

namespace hle_audio {
namespace rt {

struct buffer_data_source_t {
    ma_data_source_base base;

    decoder_t decoder;
    ma_format format;
    file_data_t::meta_t meta;
    data_buffer_t buffer;

    ma_uint64 read_cursor;

    // output
    data_buffer_t read_buffer;
    uint64_t read_bytes;

    ma_uint64 skip_read_bytes;
};


struct buffer_data_source_init_info_t {
    decoder_t decoder;
    ma_format format;
    file_data_t::meta_t meta;
    data_buffer_t buffer;
};

ma_result buffer_data_source_init(buffer_data_source_t* ds, const buffer_data_source_init_info_t& info);
void buffer_data_source_uninit(buffer_data_source_t* ds);

}
}
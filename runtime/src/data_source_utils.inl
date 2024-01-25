#pragma once

#include "miniaudio_public.h"

namespace hle_audio {
namespace rt {

static uint8_t get_sample_byte_size(ma_format format) {
    switch (format)
    {
    case ma_format_f32:
        return sizeof(float);
    case ma_format_s16:
        return sizeof(int16_t);
    
    default:
        assert(false);
        break;
    }
    return 0;
}

}
}

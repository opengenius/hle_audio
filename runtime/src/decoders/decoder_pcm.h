#pragma once

#include "decoder.h"
#include "internal_alloc_types.h"

namespace hle_audio {
namespace rt {

struct pcm_decoder_t;

struct pcm_decoder_create_info_t {
    allocator_t allocator;
};

pcm_decoder_t* create_decoder(const pcm_decoder_create_info_t& info);
void destroy(pcm_decoder_t* dec);

void reset(pcm_decoder_t* dec);
decoder_t cast_to_decoder(pcm_decoder_t* dec);

}
}

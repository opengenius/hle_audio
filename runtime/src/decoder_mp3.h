#pragma once

#include "decoder.h"
#include "internal_alloc_types.h"
#include "internal_jobs_types.h"

namespace hle_audio {
namespace rt {

struct mp3_decoder_t;

struct mp3_decoder_create_info_t {
    allocator_t allocator;
    jobs_t jobs;
};

mp3_decoder_t* create_decoder(const mp3_decoder_create_info_t& info);
void destroy(mp3_decoder_t* dec);

bool is_running(const mp3_decoder_t* dec);

decoder_t cast_to_decoder(mp3_decoder_t* dec);

}
}

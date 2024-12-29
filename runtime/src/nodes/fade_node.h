#pragma once

#include "miniaudio_public.h"

namespace hle_audio {
namespace rt {

struct fade_graph_node_t {
    ma_node_base baseNode;
    
    ma_uint32 start_time_pcm_frames;
    ma_uint32 end_time_pcm_frames;
    ma_uint32 end_time_length_pcm;
    ma_sound* target_sound;
};

struct fade_node_init_info_t {
    ma_uint32 start_time_pcm_frames;
    ma_uint32 end_time_pcm_frames;
    ma_uint32 end_time_length_pcm;
    ma_sound* target_sound;
};

ma_result fade_node_init(ma_node_graph* pNodeGraph, const fade_node_init_info_t* init_info, fade_graph_node_t* p_node);
void fade_node_uninit(fade_graph_node_t* p_node);

}
}

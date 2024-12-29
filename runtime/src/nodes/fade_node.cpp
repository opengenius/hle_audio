#include "fade_node.h"
#include <algorithm>
#include <cassert>

namespace hle_audio {
namespace rt {

static void fade_node_process_pcm_frames(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut) {
    fade_graph_node_t* fade_node = (fade_graph_node_t*)pNode;

    ma_uint64 node_local_time = ma_node_get_time(pNode);
    ma_uint64 target_local_time = ma_node_get_time(fade_node->target_sound);

    const float* pFramesInF32  = (const float*)ppFramesIn[0];
    /* */ float* pFramesOutF32 = (      float*)ppFramesOut[0];
    assert(pFramesInF32 == pFramesOutF32);

    if (target_local_time == 0) {
        // not playing
        return;
    }

    ma_uint32 frameCount = *pFrameCountOut;
    assert(frameCount <= target_local_time);
    ma_uint64 local_time = target_local_time - frameCount;

    auto channels = ma_node_get_output_channels(pNode, 0);
    if (local_time <= fade_node->start_time_pcm_frames) {
        for (ma_uint64 iFrame = 0; iFrame < frameCount; iFrame += 1) {
            float a = std::min(ma_uint32(local_time + iFrame), fade_node->start_time_pcm_frames) / float(fade_node->start_time_pcm_frames);
            float volume = a;
            assert(0.0f <= volume && volume <= 1.0f);

            for (ma_uint32 iChannel = 0; iChannel < channels; iChannel += 1) {
                pFramesOutF32[iFrame*channels + iChannel] = pFramesInF32[iFrame*channels + iChannel] * volume;
            }
        }
    }
    if (fade_node->end_time_pcm_frames <= local_time) {
        for (ma_uint64 iFrame = 0; iFrame < frameCount; iFrame += 1) {
            float a = (std::max(ma_uint32(local_time + iFrame), fade_node->end_time_pcm_frames) - fade_node->end_time_pcm_frames) / float(fade_node->end_time_length_pcm);
            float volume = 1.0f - a;
            assert(0.0f <= volume && volume <= 1.0f);

            for (ma_uint32 iChannel = 0; iChannel < channels; iChannel += 1) {
                pFramesOutF32[iFrame*channels + iChannel] = pFramesInF32[iFrame*channels + iChannel] * volume;
            }
        }
    }
}

static ma_node_vtable g_fade_node_vtable =
{
    fade_node_process_pcm_frames,
    NULL,
    1,  /* 1 input channels. */
    1,  /* 1 output channel. */
    MA_NODE_FLAG_PASSTHROUGH
};

ma_result fade_node_init(ma_node_graph* pNodeGraph, const fade_node_init_info_t* init_info, fade_graph_node_t* p_node) {
    auto channels = ma_node_graph_get_channels(pNodeGraph);

    ma_node_config baseConfig = ma_node_config_init();
    baseConfig.vtable          = &g_fade_node_vtable;
    baseConfig.pInputChannels  = &channels;
    baseConfig.pOutputChannels = &channels;

    *p_node = {};
    p_node->start_time_pcm_frames = init_info->start_time_pcm_frames;
    p_node->end_time_pcm_frames = init_info->end_time_pcm_frames;
    p_node->end_time_length_pcm = init_info->end_time_length_pcm;
    p_node->target_sound = init_info->target_sound;

    const ma_allocation_callbacks* pAllocationCallbacks = nullptr; // no need to allocated anything?
    ma_result result = ma_node_init(pNodeGraph, &baseConfig, pAllocationCallbacks, &p_node->baseNode);
    if (result != MA_SUCCESS) {
        return result;
    }

    return result;
}

void fade_node_uninit(fade_graph_node_t* p_node) {
    const ma_allocation_callbacks* pAllocationCallbacks = nullptr; // no allocator was used

    /* The base node is always uninitialized first. */
    ma_node_uninit(p_node, pAllocationCallbacks);
}


}
}

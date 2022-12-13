#include "hlea/runtime.h"

#include <cassert>
#include <algorithm>
#include "miniaudio.h"
#include "alloc_utils.inl"
#include "file_api_vfs_bridge.h"
#include "node_state_stack.h"
#include "default_allocator.h"
#include "rt_types.h"

using namespace hle_audio::rt;
using hle_audio::node_state_stack_t;

static const uint16_t MAX_SOUNDS = 1024;
static const uint16_t MAX_ACTIVE_GROUPS = 128;
static const uint16_t SOUNDS_UNUSED_LIST = 0u;
static const uint8_t MAX_OUPUT_BUSES = 8u;

using sound_index_t = uint16_t;

enum sound_id_t : uint16_t;
const sound_id_t invalid_sound_id = (sound_id_t)0u;

struct sound_data_t {
    ma_decoder    decoder;
    ma_sound      engine_sound;
};

struct file_buffer_data_t {
    void* data;
    size_t size;
};

struct hlea_event_bank_t {
    buffer_t data_buffer_ptr;
    const hle_audio::rt::store_t* static_data;
    file_buffer_data_t file_buffers[1]; // todo: merge file buffers with static_data into single bank file
};

struct sequence_rt_state_t {
    uint32_t current_index;
};

struct repeat_rt_state_t {
    uint32_t iteration_counter;
};

enum class playing_state_e {
    PLAYING,
    PAUSED,
    STOPPED
};

struct group_data_t {
    hlea_event_bank_t* bank;
    uint32_t group_index; // index in bank
    uint32_t obj_id;

    playing_state_e state;

    sound_id_t sound_id;
    sound_id_t next_sound_id;

    bool apply_sound_fade_out;

    node_state_stack_t state_stack;
};

struct event_desc_t {
    hlea_event_bank_t* bank;
    uint32_t target_index;
    uint32_t obj_id;
    float fade_time;
};

struct hlea_context_t {
    vfs_bridge_t vfs_impl;
    allocator_t allocator;

    ma_engine engine;

    ma_sound_group output_bus_groups[MAX_OUPUT_BUSES];
    uint8_t output_bus_group_count;

    sound_data_t sounds[MAX_SOUNDS];
    uint16_t sounds_allocated;
    
    sound_index_t recycled_sound_indices[MAX_SOUNDS];
    uint16_t recycled_count;

    group_data_t active_groups[MAX_ACTIVE_GROUPS];
    uint16_t active_groups_size;
};

static void* allocator_bridge_malloc(size_t sz, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    return allocate(*alloc, sz);
}

static void* allocator_bridge_realloc(void* p, size_t sz, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    return reallocate(*alloc, p, sz);
}

static void allocator_bridge_free(void* p, void* pUserData) {
    auto alloc = (allocator_t*)pUserData;
    deallocate(*alloc, p);
}

static const ma_allocation_callbacks g_allocator_bridge_ma_cb_prototype = {
    nullptr,
    allocator_bridge_malloc,
    allocator_bridge_realloc,
    allocator_bridge_free
};

// editor statics
static ma_sound s_file_sound;
static bool s_file_sound_inited;
static const char* s_sounds_path; // file path prefix to use in file loading

/////////////////////////////////////////////////////////////////////////////////////////

static sound_data_t* get_sound_data(hlea_context_t* ctx, sound_id_t sound_id) {
    return &ctx->sounds[sound_id - 1];
}

static sound_id_t acquire_sound(hlea_context_t* ctx, sound_data_t** out_sound_ptr) {
    assert(ctx);
    assert(out_sound_ptr);

    sound_index_t sound_index = 0u;
    if (ctx->recycled_count) {
        sound_index = ctx->recycled_sound_indices[--ctx->recycled_count];
    } else {
        if (ctx->sounds_allocated == MAX_SOUNDS) return (sound_id_t)0u;
        sound_index = ctx->sounds_allocated++;
    }

    sound_id_t sound_id = (sound_id_t)(sound_index + 1);

    sound_data_t* data_ptr = get_sound_data(ctx, sound_id);

    const sound_data_t null_data = {};
    *data_ptr = null_data;

    *out_sound_ptr = data_ptr;
    return sound_id;
}

static void release_sound(hlea_context_t* ctx, sound_id_t sound_id) {
    assert(ctx);
    assert(sound_id);

    sound_index_t sound_index = sound_id - 1;

    assert(ctx->recycled_count < MAX_SOUNDS);
    ctx->recycled_sound_indices[ctx->recycled_count++] = sound_index;
}

static uint32_t scan_uint32(const uint8_t** data_ptr) {
    uint32_t value;
    memcpy(&value, *data_ptr, sizeof(value));
    *data_ptr += sizeof(value);
    return value;
}

/**
 * Extracts loop info from smpl subchunk of WAVE riff file
 */
static bool find_wav_loop_point(const file_buffer_data_t& buffer_data, uint64_t* out_loop_start, uint64_t* out_loop_end) {
    union tag_t {
        char ansi[4];
        uint32_t single;
    };

    const tag_t RIFF_tag = {'R', 'I', 'F', 'F'};
    const tag_t WAVE_tag = {'W', 'A', 'V', 'E'};
    const tag_t smpl_tag = {'s', 'm', 'p', 'l'};

    auto data_ptr = (const uint8_t*)buffer_data.data;

    uint32_t riff_id = scan_uint32(&data_ptr);
    if (riff_id != RIFF_tag.single) return false;

    // move to format
    data_ptr += sizeof(uint32_t);

    uint32_t fortat_tag = scan_uint32(&data_ptr);
    if (fortat_tag != WAVE_tag.single) return false;

    // chunks
    auto buffer_end_ptr = (const uint8_t*)buffer_data.data + buffer_data.size;
    while(data_ptr < buffer_end_ptr) {
        uint32_t chunk_tag = scan_uint32(&data_ptr);
        uint32_t chunk_size = scan_uint32(&data_ptr);

        if (chunk_tag == smpl_tag.single) {
            uint32_t loop_count = 0;
            memcpy(&loop_count, data_ptr + 28, sizeof(loop_count));

            if (loop_count) {
                uint32_t loop_start, loop_end;
                memcpy(&loop_start, data_ptr + 44, sizeof(loop_start));
                memcpy(&loop_end, data_ptr + 48, sizeof(loop_end));

                *out_loop_start = loop_start;
                *out_loop_end = loop_end;
                return true;
            }

            // smpl chunk found, no need to look more
            break;
        }
        
        data_ptr += chunk_size;
    }

    return false;
}

extern "C" ma_uint64 ma_calculate_frame_count_after_resampling(ma_uint32 sampleRateOut, ma_uint32 sampleRateIn, ma_uint64 frameCountIn);

static ma_result read_file(ma_vfs* pVFS, const char* pFilePath, 
        const allocator_t& alloc,
        file_buffer_data_t* out_read_buffer) {
    ma_result result;
    ma_vfs_file file;
    ma_file_info info;

    result = ma_vfs_open(pVFS, pFilePath, MA_OPEN_MODE_READ, &file);
    if (result != MA_SUCCESS) {
        return result;
    }

    result = ma_vfs_info(pVFS, file, &info);
    if (result != MA_SUCCESS) {
        ma_vfs_close(pVFS, file);
        return result;
    }

    if (info.sizeInBytes > MA_SIZE_MAX) {
        ma_vfs_close(pVFS, file);
        return MA_TOO_BIG;
    }

    file_buffer_data_t res = {};
    res.data = allocate(alloc, info.sizeInBytes);
    if (res.data == NULL) {
        ma_vfs_close(pVFS, file);
        return result;
    }

    result = ma_vfs_read(pVFS, file, res.data, (size_t)info.sizeInBytes, &res.size);  /* Safe cast. */
    ma_vfs_close(pVFS, file);

    if (result != MA_SUCCESS) {
        deallocate(alloc, res.data);
        return result;
    }

    assert(out_read_buffer != NULL);
    *out_read_buffer = res;

    return MA_SUCCESS;
}

static sound_id_t make_sound(hlea_context_t* ctx, 
        hlea_event_bank_t* bank, uint8_t output_bus_index,
        const hle_audio::rt::file_node_t* file_node) {
    const sound_id_t invalid_id = (sound_id_t)0u;

    const ma_uint32 sound_flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;

    auto buf_ptr = bank->data_buffer_ptr;

    if (file_node->stream) {
        const char* path = bank->static_data->sound_files.get(buf_ptr, file_node->file_index).get_ptr(buf_ptr);
        
        sound_data_t* sound;
        auto sound_id = acquire_sound(ctx, &sound);
        if (!sound_id) {
            // sound limit reached
            return invalid_id;
        }

        char path_buf[512];
        if (s_sounds_path) {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", s_sounds_path, path);
            path = path_buf;
        }

        ma_sound_init_from_file(&ctx->engine,
                path, sound_flags | MA_SOUND_FLAG_STREAM, 
                &ctx->output_bus_groups[output_bus_index],
                nullptr,
                &sound->engine_sound);

        ma_sound_set_looping(&sound->engine_sound, file_node->loop);

        return sound_id;
    }

    auto& buffer_data = bank->file_buffers[file_node->file_index];
    if (!buffer_data.data) {
        ma_vfs* vfs = ctx->engine.pResourceManager->config.pVFS;

        const char* path = bank->static_data->sound_files.get(buf_ptr, file_node->file_index).get_ptr(buf_ptr);

        // todo: unsafe
        char path_buf[512];
        if (s_sounds_path) {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", s_sounds_path, path);
            path = path_buf;
        }

        file_buffer_data_t read_buffer;
        ma_result result = read_file(vfs, path, 
                                ctx->allocator, &read_buffer);
        if (result == MA_SUCCESS) {
            buffer_data = read_buffer;
        }
    }
    if (!buffer_data.data) return invalid_id;

    sound_data_t* sound;
    auto sound_id = acquire_sound(ctx, &sound);
    if (!sound_id) {
        // sound limit reached
        return invalid_id;
    }

    const auto& rm_config = ctx->engine.pResourceManager->config;

    //
    // Init decoder
    //
    const ma_decoder_config decoder_config = ma_decoder_config_init(
        rm_config.decodedFormat, 
        rm_config.decodedChannels, 
        rm_config.decodedSampleRate);
    auto result = ma_decoder_init_memory(buffer_data.data, buffer_data.size, &decoder_config, &sound->decoder);
    if (result != MA_SUCCESS) {
        release_sound(ctx, sound_id);
        return invalid_id;  // Failed to init decoder
    }

    //
    // Init sound
    //
    result = ma_sound_init_from_data_source(&ctx->engine, 
            &sound->decoder,
            sound_flags | MA_SOUND_FLAG_ASYNC, 
            &ctx->output_bus_groups[output_bus_index], 
            &sound->engine_sound);

    if (result != MA_SUCCESS) {
        ma_decoder_uninit(&sound->decoder);
        release_sound(ctx, sound_id);
        return invalid_id;  // Failed to init sound.
    }

    ma_sound_set_looping(&sound->engine_sound, file_node->loop);

    // Extract wav loop point
    uint64_t loop_start, loop_end;
    if (find_wav_loop_point(buffer_data, &loop_start, &loop_end)) {
        ma_result result;
        ma_uint32 internalSampleRate;

        result = ma_data_source_get_data_format(sound->decoder.pBackend, NULL, NULL, &internalSampleRate, NULL, 0);
        if (result == MA_SUCCESS) {
            loop_start = ma_calculate_frame_count_after_resampling(rm_config.decodedSampleRate, internalSampleRate, loop_start);
            loop_end = ma_calculate_frame_count_after_resampling(rm_config.decodedSampleRate, internalSampleRate, loop_end);

            /**
             * Internal decoder loop frames have a slight bias
             * (1 frame at least) because of resampling calculation.
             * todo: check if this is a problem
             */
            ma_data_source_set_loop_point_in_pcm_frames(&sound->decoder, loop_start, loop_end);            
        }
    }

    return sound_id;
}

template<typename T>
inline memory_layout_t static_type_layout() {
    return {sizeof(T), alignof(T)};
}

static bool get_state_info(const node_desc_t* node_desc, memory_layout_t* out_layout) {
    if (node_desc->type == node_type_e::Sequence) {
        *out_layout = static_type_layout<sequence_rt_state_t>();
        return true;
    } else if (node_desc->type == node_type_e::Repeat) {
        *out_layout = static_type_layout<repeat_rt_state_t>();
        return true;
    }
    return false;
}

static void init_state(const node_desc_t* node_desc, void* state) {
    if (node_desc->type == node_type_e::Sequence) {
        auto s = (sequence_rt_state_t*)state;
        *s = {};
    } else if (node_desc->type == node_type_e::Repeat) {
        auto s = (repeat_rt_state_t*)state;
        *s = {};
    }
}

template<typename T>
static const T* bank_get(const hlea_event_bank_t* bank, 
        const array_view_t<T> arr, size_t index) {
    auto data_ptr = bank->data_buffer_ptr;
    return &arr.get(data_ptr, index);
}

static const named_group_t* bank_get_group(const hlea_event_bank_t* bank, uint32_t group_index) {
    return bank_get(bank, bank->static_data->groups, group_index);
}

static const node_desc_t* next_node(const hlea_event_bank_t* bank, const node_desc_t* node_desc, void* state) {
    if (node_desc->type == node_type_e::Sequence) {
        auto s = (sequence_rt_state_t*)state;
        
        auto seq_node = bank_get(bank, bank->static_data->nodes_sequence, node_desc->index);

        auto seq_size = seq_node->nodes.count;
        if (seq_size <= s->current_index) return nullptr;

        auto res = &seq_node->nodes.get(bank->data_buffer_ptr, s->current_index);

        // update state
        ++s->current_index;

        return res;
    } else if (node_desc->type == node_type_e::Repeat) {
        auto s = (repeat_rt_state_t*)state;
        
        auto repeat_node = bank_get(bank, bank->static_data->nodes_repeat, node_desc->index);

        if (repeat_node->repeat_count && repeat_node->repeat_count <= s->iteration_counter) return nullptr;

        auto res = &repeat_node->node;

        // update state
        ++s->iteration_counter;

        return res;

    //
    // stateless nodes
    //
    } else if (node_desc->type == node_type_e::Random) {
        auto random_node = bank_get(bank, bank->static_data->nodes_random, node_desc->index);
        int random_index = rand() % random_node->nodes.count;
        return &random_node->nodes.get(bank->data_buffer_ptr, random_index);
    }

    return nullptr;
}

static bool init_and_push_state(node_state_stack_t& stack, const node_desc_t* node_desc) {
    memory_layout_t layout =  {};
    if (get_state_info(node_desc, &layout)) {
        push_state(stack, node_desc, layout);
        init_state(node_desc, stack.top_entry->state_data);

        return true;
    }
    return false;
}

static const node_desc_t* next_node_statefull(node_state_stack_t& stack, const hlea_event_bank_t* bank) {
    const node_desc_t* next_node_desc = nullptr;
    while (!next_node_desc && stack.top_entry) {
        next_node_desc = next_node(bank, stack.top_entry->node_desc, stack.top_entry->state_data);
        if (!next_node_desc) pop_up_state(stack);
    }

    return next_node_desc;
}

static sound_id_t make_next_sound(hlea_context_t* ctx, group_data_t& group) {
    auto group_sdata = bank_get_group(group.bank, group.group_index);

    const node_desc_t* next_node_desc = nullptr;
    // first run
    if (!group.state_stack.top_entry) {
        next_node_desc = &group_sdata->node;
    }

    while(true) {
        if (!next_node_desc) {
            // process state node
            next_node_desc = next_node_statefull(group.state_stack, group.bank);
        }

        // no nodes to process, finish
        if (!next_node_desc) break;

        // sound node found, interupt traversing, generate sound
        if (next_node_desc->type == node_type_e::File) {
            auto file_node = bank_get(group.bank, group.bank->static_data->nodes_file, next_node_desc->index);
            return make_sound(ctx, group.bank, group_sdata->output_bus_index, file_node);
        }

        if (init_and_push_state(group.state_stack, next_node_desc)) {
            // use stacked node
            next_node_desc = nullptr;
        } else {
            // stateless
            next_node_desc = next_node(group.bank, next_node_desc, nullptr);
        }
    }

    return invalid_sound_id;
}

static void start_next_after_current(hlea_context_t* ctx, group_data_t& group) {
    assert(group.sound_id);
    if (!group.next_sound_id) return;

    auto sound_data_ptr = get_sound_data(ctx, group.sound_id);
    auto sound = &sound_data_ptr->engine_sound;
    if (!ma_sound_is_looping(sound)) {

        ma_uint64 cursor, length;
        ma_sound_get_cursor_in_pcm_frames(sound, &cursor);
        ma_sound_get_length_in_pcm_frames(sound, &length);

        auto sound_finished = ma_sound_at_end(sound);

        auto group_data = bank_get_group(group.bank, group.group_index);

        auto engine_srate = ma_engine_get_sample_rate(&ctx->engine);
        auto fade_time_pcm = (ma_uint64)(group_data->cross_fade_time * engine_srate);

        auto next_sound_data_ptr = get_sound_data(ctx, group.next_sound_id);
        auto next_sound = &next_sound_data_ptr->engine_sound;
        if (fade_time_pcm) {
            ma_sound_set_fade_in_pcm_frames(next_sound, -1, 1, fade_time_pcm);
        }
        if (!sound_finished && (cursor + fade_time_pcm < length)) {
            ma_sound_set_start_time_in_pcm_frames(next_sound, ma_engine_get_time(&ctx->engine) - cursor + length - fade_time_pcm);
        }
        ma_sound_set_stop_time_in_pcm_frames(next_sound, (ma_uint64)-1);
        ma_sound_start(next_sound);

        if (!sound_finished) {
            group.apply_sound_fade_out = true;
        }
    }
}

static void group_make_next_sound(hlea_context_t* ctx, group_data_t& group) {
    group.next_sound_id = invalid_sound_id;

    // no state left, leave
    if (!group.state_stack.top_entry) return;

    group.next_sound_id = make_next_sound(ctx, group);
    if (group.next_sound_id) {
        auto group_data = bank_get_group(group.bank, group.group_index);

        auto next_sound_data_ptr = get_sound_data(ctx, group.next_sound_id);
        ma_sound_set_volume(&next_sound_data_ptr->engine_sound, group_data->volume);

        start_next_after_current(ctx, group);
    }
}

static void group_play(hlea_context_t* ctx, const event_desc_t* desc) {
    if (ctx->active_groups_size == MAX_ACTIVE_GROUPS) return;

    group_data_t group = {};
    group.bank = desc->bank;
    group.group_index = desc->target_index;
    group.obj_id = desc->obj_id;
    // todo: use pooled allocator instead of general one
    // todo: control size
    init(group.state_stack, 256, ctx->allocator);

    group.sound_id = make_next_sound(ctx, group);
    if (group.sound_id) {
        auto sound_data_ptr = get_sound_data(ctx, group.sound_id);
        auto sound = &sound_data_ptr->engine_sound;

        auto group_data = bank_get_group(desc->bank, desc->target_index);

        ma_sound_set_volume(sound, group_data->volume);
        if (0 < desc->fade_time) {
            ma_sound_set_fade_in_milliseconds(sound, 0, 1, desc->fade_time * 1000);
        }
        ma_sound_start(sound);

        group_make_next_sound(ctx, group);
    }

    ctx->active_groups[ctx->active_groups_size++] = group;
}

static uint32_t find_active_group_index(hlea_context_t* ctx, const event_desc_t* desc) {
    uint32_t index = 0u;
    for (size_t it_index = 0u; it_index < ctx->active_groups_size; ++it_index) {
        auto& group = ctx->active_groups[it_index];

        if (group.bank == desc->bank &&
            group.group_index == desc->target_index &&
            group.obj_id == desc->obj_id) {
            break;
        }
        ++index;
    }
    return index;
}

static void group_play_single(hlea_context_t* ctx, const event_desc_t* desc) {
    auto active_index = find_active_group_index(ctx, desc);

    if (active_index < ctx->active_groups_size) return;

    group_play(ctx, desc);
}

static void sound_fade_and_stop(hlea_context_t* ctx, sound_id_t sound_id, ma_uint64 fade_time_pcm) {
    if (!sound_id) return;

    auto sound_data = get_sound_data(ctx, sound_id);

    // just stop if not playing (could have delayed start, so need to stop explicitly)
    if (!ma_sound_is_playing(&sound_data->engine_sound)) {
        ma_sound_stop(&sound_data->engine_sound);
        return;
    }

    // schedule fade
    ma_sound_set_fade_in_pcm_frames(&sound_data->engine_sound, -1, 0, fade_time_pcm);
    
    // schedule stop
    auto engine = ma_sound_get_engine(&sound_data->engine_sound);
    auto engine_time = ma_engine_get_time(engine);
    ma_sound_set_stop_time_in_pcm_frames(&sound_data->engine_sound, engine_time + fade_time_pcm);
}

static void group_active_stop_with_fade(hlea_context_t* ctx, group_data_t& group, float fade_time) {
    if (group.state == playing_state_e::STOPPED) return;
    group.state = playing_state_e::STOPPED;

    auto engine_srate = ma_engine_get_sample_rate(&ctx->engine);
    auto fade_time_pcm = (ma_uint64)(fade_time * engine_srate);

    sound_fade_and_stop(ctx, group.sound_id, fade_time_pcm);
    sound_fade_and_stop(ctx, group.next_sound_id, fade_time_pcm);
}

static void group_stop(hlea_context_t* ctx, const event_desc_t* desc) {
    auto active_index = find_active_group_index(ctx, desc);

    if (active_index == ctx->active_groups_size) return;

    group_active_stop_with_fade(ctx, ctx->active_groups[active_index], desc->fade_time);
}


static void group_active_pause_with_fade(hlea_context_t* ctx, group_data_t& group, float fade_time) {
    if (group.state != playing_state_e::PLAYING) return;
    group.state = playing_state_e::PAUSED;

    auto engine_srate = ma_engine_get_sample_rate(&ctx->engine);
    auto fade_time_pcm = (ma_uint64)(fade_time * engine_srate);

    sound_fade_and_stop(ctx, group.sound_id, fade_time_pcm);
    sound_fade_and_stop(ctx, group.next_sound_id, fade_time_pcm);
}

static void group_pause(hlea_context_t* ctx, const event_desc_t* desc) {
    auto active_index = find_active_group_index(ctx, desc);

    if (active_index == ctx->active_groups_size) return;

    group_active_pause_with_fade(ctx, ctx->active_groups[active_index], desc->fade_time);
}

static void sound_start_with_fade(hlea_context_t* ctx, sound_id_t sound_id, ma_uint64 fade_time_pcm) {
    if (!sound_id) return;

    auto sound_data = get_sound_data(ctx, sound_id);
    auto sound = &sound_data->engine_sound;

    // finished, nothing to resume
    if (ma_sound_at_end(sound)) return;

    // disable stop timer
    ma_sound_set_stop_time_in_pcm_frames(sound, (ma_uint64)-1);

    // setup fade
    ma_sound_set_fade_in_pcm_frames(sound, -1, 1, fade_time_pcm);

    // and start
    ma_sound_start(sound);
}

static void group_active_resume_with_fade(hlea_context_t* ctx, group_data_t& group, float fade_time) {
    if (group.state != playing_state_e::PAUSED) return;
    group.state = playing_state_e::PLAYING;

    auto engine_srate = ma_engine_get_sample_rate(&ctx->engine);
    auto fade_time_pcm = (ma_uint64)(fade_time * engine_srate);

    sound_start_with_fade(ctx, group.sound_id, fade_time_pcm);
    start_next_after_current(ctx, group);
}

static void group_resume(hlea_context_t* ctx, const event_desc_t* desc) {
    auto active_index = find_active_group_index(ctx, desc);

    if (active_index == ctx->active_groups_size) return;

    group_active_resume_with_fade(ctx, ctx->active_groups[active_index], desc->fade_time);
}

static void group_stop_all(hlea_context_t* ctx, const event_desc_t* desc) {
    for (size_t it_index = 0u; it_index < ctx->active_groups_size; ++it_index) {
        auto& group = ctx->active_groups[it_index];

        if (group.obj_id == desc->obj_id) {
            group_active_stop_with_fade(ctx, group, desc->fade_time);
        }
    }
}

typedef void (*group_with_fade_func)(hlea_context_t* ctx, group_data_t& group, float fade_time);

static void apply_to_groups_with_bus(hlea_context_t* ctx, const event_desc_t* desc, group_with_fade_func action_func) {
    for (size_t it_index = 0u; it_index < ctx->active_groups_size; ++it_index) {
        auto& group = ctx->active_groups[it_index];

        auto group_data = bank_get_group(group.bank, group.group_index);

        if (group_data->output_bus_index == desc->target_index) {
            action_func(ctx, group, desc->fade_time);
        }
    }
}

static void group_stop_bus(hlea_context_t* ctx, const event_desc_t* desc) {
    apply_to_groups_with_bus(ctx, desc, group_active_stop_with_fade);
}

static void group_pause_bus(hlea_context_t* ctx, const event_desc_t* desc) {
    apply_to_groups_with_bus(ctx, desc, group_active_pause_with_fade);
}

static void group_resume_bus(hlea_context_t* ctx, const event_desc_t* desc) {
    apply_to_groups_with_bus(ctx, desc, group_active_resume_with_fade);
}

static void group_break_loop(hlea_context_t* ctx, const event_desc_t* desc) {
    auto active_index = find_active_group_index(ctx, desc);

    if (active_index == ctx->active_groups_size) return;

    group_data_t& group = ctx->active_groups[active_index];
    auto sound_data_ptr = get_sound_data(ctx, group.sound_id);
    ma_sound_set_looping(&sound_data_ptr->engine_sound, false);
    
    start_next_after_current(ctx, group);
}

static void uninit_and_release_sound(hlea_context_t* ctx, sound_id_t sound_id) {
    auto sound_data_ptr = get_sound_data(ctx, sound_id);
    
    ma_sound_uninit(&sound_data_ptr->engine_sound);
    ma_decoder_uninit(&sound_data_ptr->decoder);

    release_sound(ctx, sound_id);
}

static void group_active_release(hlea_context_t* ctx, uint32_t active_index) {
    group_data_t& group = ctx->active_groups[active_index];

    // clean up state data
    deinit(group.state_stack);

    // swap remove
    ctx->active_groups[active_index] = ctx->active_groups[ctx->active_groups_size - 1];
    --ctx->active_groups_size;
}


hlea_context_t* hlea_create(hlea_context_create_info_t* info) {

    allocator_t alloc = hle_audio::make_default_allocator();
    if (info->allocator_vt) {
        alloc = allocator_t{info->allocator_vt, info->allocator_udata};
    }

    auto ctx = allocate_unique<hlea_context_t>(alloc);
    memset(ctx.get(), 0, sizeof(hlea_context_t));
    ctx->allocator = alloc;

    auto config = ma_engine_config_init();
    if (info->file_api_vt) {
        init(ctx->vfs_impl, info->file_api_vt, info->file_sys);
        config.pResourceManagerVFS = &ctx->vfs_impl;
    }
    config.allocationCallbacks = g_allocator_bridge_ma_cb_prototype;
    config.allocationCallbacks.pUserData = &ctx->allocator;

    ma_result result = ma_engine_init(&config, &ctx->engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize audio engine.");

        return nullptr;
    }

    ctx->output_bus_group_count = (info->output_bus_count <= MAX_OUPUT_BUSES) ? info->output_bus_count : MAX_OUPUT_BUSES;
    for (size_t i = 0; i < ctx->output_bus_group_count; ++i) {
        // todo: check results, deinit, return nullptr
        result = ma_sound_group_init(&ctx->engine, 0, nullptr, &ctx->output_bus_groups[i]);
    }

    return ctx.release();
}

void hlea_destroy(hlea_context_t* ctx) {
    for (size_t i = 0; i < ctx->output_bus_group_count; ++i) {
        ma_sound_group_uninit(&ctx->output_bus_groups[i]);
    }
    ma_engine_uninit(&ctx->engine);

    deallocate(ctx->allocator, ctx);
}

void hlea_suspend(hlea_context_t* ctx) {
    ma_engine_stop(&ctx->engine);
}

void hlea_wakeup(hlea_context_t* ctx) {
    ma_engine_start(&ctx->engine);
}

/**
 * init with allocated buffer
 */
static hlea_event_bank_t* load_events_bank_buffer(hlea_context_t* ctx, void* pData) {
    auto data_header = (root_header_t*)pData;
    if (data_header->version != STORE_BLOB_VERSION) {
        deallocate(ctx->allocator, pData);
        return nullptr;
    }

    buffer_t buf = {};
    buf.ptr = pData;

    auto store = data_header->store.get_ptr(buf);

    auto files_size = store->sound_files.count;

    size_t bank_byte_size = offsetof(struct hlea_event_bank_t, file_buffers[files_size]);
    auto bank = (hlea_event_bank_t*)allocate(ctx->allocator, bank_byte_size, alignof(hlea_event_bank_t));

    bank->data_buffer_ptr = buf;
    bank->static_data = store;
    memset(bank->file_buffers, 0, sizeof(file_buffer_data_t) * files_size);

    return bank;
}

hlea_event_bank_t* hlea_load_events_bank(hlea_context_t* ctx, const char* bank_filename) {
    ma_vfs* vfs = ctx->engine.pResourceManager->config.pVFS;

    file_buffer_data_t buffer = {};
    ma_result result = read_file(vfs, bank_filename, ctx->allocator, &buffer);

    return load_events_bank_buffer(ctx, buffer.data);
}

hlea_event_bank_t* hlea_load_events_bank_from_buffer(hlea_context_t* ctx, const uint8_t* buf, size_t buf_size) {
    auto internal_buf = allocate(ctx->allocator, buf_size);
    memcpy(internal_buf, buf, buf_size);

    return load_events_bank_buffer(ctx, internal_buf);
}

void hlea_unload_events_bank(hlea_context_t* ctx, hlea_event_bank_t* bank) {
    // stop all sounds from bank
    for (uint32_t active_index = 0u; active_index < ctx->active_groups_size; ++active_index) {
        group_data_t& group = ctx->active_groups[active_index];
        if (group.bank != bank) continue;

        if (group.sound_id) uninit_and_release_sound(ctx, group.sound_id);
        if (group.next_sound_id) uninit_and_release_sound(ctx, group.next_sound_id);
        group_active_release(ctx, active_index);
        --active_index;
    }

    // release file buffers
    auto files_size = bank->static_data->sound_files.count;
    for (uint32_t i = 0u; i < files_size; ++i) {
        deallocate(ctx->allocator, bank->file_buffers[i].data);
    }

    deallocate(ctx->allocator, bank->data_buffer_ptr.ptr);
    deallocate(ctx->allocator, bank);
}

void hlea_process_active_groups(hlea_context_t* ctx) {
    for (uint32_t active_index = 0u; active_index < ctx->active_groups_size; ++active_index) {
        group_data_t& group = ctx->active_groups[active_index];

        // skip paused groups
        // (todo(optimization): move to paused/inactive queue?)
        if (group.state == playing_state_e::PAUSED) continue;

        if (group.sound_id) {
            auto sound_data_ptr = get_sound_data(ctx, group.sound_id);

            // apply fade out when it's time
            if (group.apply_sound_fade_out) {
                ma_uint64 cursor, length;
                ma_sound_get_cursor_in_pcm_frames(&sound_data_ptr->engine_sound, &cursor);
                ma_sound_get_length_in_pcm_frames(&sound_data_ptr->engine_sound, &length);

                auto group_data = bank_get_group(group.bank, group.group_index);
                auto engine_srate = ma_engine_get_sample_rate(&ctx->engine);
                auto fade_time_pcm = (ma_uint64)(group_data->cross_fade_time * engine_srate);

                if (length <= cursor + fade_time_pcm) {
                    group.apply_sound_fade_out = false;
                    ma_sound_set_fade_in_pcm_frames(&sound_data_ptr->engine_sound, -1, 0, length - cursor);
                }
            }

            // if stopped, just wait to finish playing and clean up then
            if (group.state == playing_state_e::STOPPED) {

                while(group.sound_id) {
                    auto sound_data_ptr = get_sound_data(ctx, group.sound_id);
                    if (!ma_sound_is_playing(&sound_data_ptr->engine_sound)) {
                        uninit_and_release_sound(ctx, group.sound_id);
                        group.sound_id = group.next_sound_id;
                        group.next_sound_id = invalid_sound_id;
                    } else break;
                }

            // if playing 
            } else if (ma_sound_at_end(&sound_data_ptr->engine_sound)) {
                uninit_and_release_sound(ctx, group.sound_id);

                group.sound_id = group.next_sound_id;
                group_make_next_sound(ctx, group);
            }
        }

        if (!group.sound_id) {
            group_active_release(ctx, active_index);
            --active_index;
        }
    }
}

static void fire_event(hlea_context_t* ctx, hlea_action_type_e event_type, const event_desc_t* desc) {
    switch(event_type) {
        case hlea_action_type_e::play: {
            group_play(ctx, desc);
            break;
        }
        case hlea_action_type_e::play_single: {
            group_play_single(ctx, desc);
            break;
        }
        case hlea_action_type_e::stop: {
            group_stop(ctx, desc);
            break;
        }
        case hlea_action_type_e::pause: {
            group_pause(ctx, desc);
            break;
        }
        case hlea_action_type_e::resume: {
            group_resume(ctx, desc);
            break;
        }
        case hlea_action_type_e::stop_all: {
            group_stop_all(ctx, desc);
            break;
        }
        case hlea_action_type_e::break_loop: {
            group_break_loop(ctx, desc);
            break;
        }
        case hlea_action_type_e::stop_bus: {
            group_stop_bus(ctx, desc);
            break;
        }
        case hlea_action_type_e::pause_bus: {
            group_pause_bus(ctx, desc);
            break;
        }
        case hlea_action_type_e::resume_bus: {
            group_resume_bus(ctx, desc);
            break;
        }
    }
}

void hlea_fire_event(hlea_context_t* ctx, hlea_event_bank_t* bank, const char* eventName, uint32_t obj_id) {
    // find event with binary search
    // todo: replace with hash index
    auto buf_ptr = bank->data_buffer_ptr;
    auto event_offset_begin = bank->static_data->events.elements.get_ptr(buf_ptr);
    auto event_offset_end = event_offset_begin + bank->static_data->events.count;
    auto event = std::lower_bound(event_offset_begin, event_offset_end,
            eventName, [buf_ptr](const event_t& event, const char* str)
            {
                auto event_name = event.name.get_ptr(buf_ptr);
                return strcmp(event_name, str) < 0;
            });
    if (event == event_offset_end) return;

    auto actions_size = event->actions.count;
    auto actions = event->actions.elements.get_ptr(buf_ptr);
    for (uint32_t action_index = 0u; action_index < actions_size; ++action_index) {
        auto action = &actions[action_index];
        if (action->type == action_type_e::none) continue;

        event_desc_t desc = {};
        desc.bank = bank;
        desc.target_index = action->target_index;
        desc.obj_id = obj_id;
        desc.fade_time = action->fade_time;

        auto type = (hlea_action_type_e)((int)(action->type) - 1);
        fire_event(ctx, type, &desc);
    }
}

void hlea_fire_event(hlea_context_t* ctx, const hlea_fire_event_info_t* event_info) {
    assert(event_info);

    for (uint32_t action_index = 0u; action_index< event_info->action_count; ++action_index) {
        auto& action = event_info->actions[action_index];

        event_desc_t desc = {};
        desc.bank = event_info->bank;
        desc.target_index = action.target_index;
        desc.obj_id = event_info->obj_id;
        desc.fade_time = action.fade_time;

        fire_event(ctx, action.type, &desc);
    }
}

void hlea_set_main_volume(hlea_context_t* ctx, float volume) {
    ma_engine_set_volume(&ctx->engine, volume);   
}

void hlea_set_bus_volume(hlea_context_t* ctx, uint8_t bus_index, float volume) {
    ma_sound_group_set_volume(&ctx->output_bus_groups[bus_index], volume);
}

/** 
 * editor api
 */


size_t hlea_get_active_groups_count(hlea_context_t* ctx) {
    return ctx->active_groups_size;
}

size_t hlea_get_active_groups_infos(hlea_context_t* ctx, hlea_group_info_t* out_infos, size_t out_infos_size) {
    const size_t out_groups_count = (out_infos_size < ctx->active_groups_size) ? out_infos_size : ctx->active_groups_size;

    for (size_t active_index = 0u; active_index < out_groups_count; ++active_index) {
        group_data_t& group = ctx->active_groups[active_index];

        auto& out_info = out_infos[active_index];
        out_info.group_index = group.group_index;
        out_info.paused = (group.state == playing_state_e::PAUSED);
    }

    return out_groups_count;
}

void hlea_set_sounds_path(hlea_context_t* ctx, const char* sounds_path) {
    s_sounds_path = sounds_path;
}

void hlea_play_file(hlea_context_t* ctx, const char* file_path) {
    hlea_stop_file(ctx);
    ma_result result = ma_sound_init_from_file(&ctx->engine, file_path, 0, NULL, NULL, &s_file_sound);
    if (result != MA_SUCCESS) {
        return;
    }
    s_file_sound_inited = true;

    ma_sound_start(&s_file_sound);
}

bool hlea_is_file_playing(hlea_context_t* ctx) {
    if (s_file_sound_inited) {
        return ma_sound_is_playing(&s_file_sound);
    }

    return false;
}

void hlea_stop_file(hlea_context_t* ctx) {
    if (s_file_sound_inited) {
        ma_sound_stop(&s_file_sound);
        ma_sound_uninit(&s_file_sound);
        s_file_sound_inited = false;
    }
}

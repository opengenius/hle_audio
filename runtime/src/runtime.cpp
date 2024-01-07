#include "hlea/runtime.h"

#include <cstring>
#include <cassert>
#include <cstdio>
#include <algorithm>

#include "miniaudio_public.h"

#include "file_api_vfs_bridge.h"
#include "default_allocator.h"
#include "decoder.h"
#include "async_file_reader.h"
#include "internal_editor_runtime.h"
#include "internal_types.h"
#include "chunk_streaming_cache.h"
#include "decoder_mp3.h"

#include "alloc_utils.inl"
#include "jobs_utils.inl"
#include "file_utils.inl"
#include "allocator_bridge.inl"

// miniaudio.h
extern "C" ma_uint64 ma_calculate_frame_count_after_resampling(ma_uint32 sampleRateOut, ma_uint32 sampleRateIn, ma_uint64 frameCountIn);

/**
 * streaming TODOs:
 *  - implement decoder_ti for wav, ogg, etc
 *      - use asbtract in streaming_data_source_t
 *      - recycle decoders instead of reiniting them
 *  - use decoder_ti with in-memory buffers (non-streaming data)
 *      - requires seeking for loop range support
 * - remove sound_files from static data (store_t)
 */

using hle_audio::rt::node_state_stack_t;
using hle_audio::rt::data_buffer_t;
using hle_audio::rt::buffer_t;
using hle_audio::rt::file_data_t;
using hle_audio::rt::array_view_t;
using hle_audio::rt::named_group_t;
using hle_audio::rt::node_desc_t;
using hle_audio::rt::node_type_e;
using hle_audio::rt::root_header_t;
using hle_audio::rt::event_t;
using hle_audio::rt::action_type_e;
using hle_audio::rt::async_file_reader_t;
using hle_audio::rt::async_file_handle_t;
using hle_audio::rt::streaming_data_source_t;
using hle_audio::rt::streaming_data_source_init_info_t;
using hle_audio::rt::range_t;
using hle_audio::rt::editor_runtime_t;
using hle_audio::rt::audio_format_type_e;
using hle_audio::rt::decoder_t;

/////////////////////////////////////////////////////////////////////////////////////////

static sound_data_t* get_sound_data(hlea_context_t* ctx, sound_id_t sound_id) {
    return &ctx->sounds[sound_id - 1];
}

static sound_id_t acquire_sound(hlea_context_t* ctx, sound_data_t** out_sound_ptr) {
    assert(ctx);
    assert(out_sound_ptr);

    sound_id_t sound_id = {};
    if (ctx->recycled_count) {
        sound_id = ctx->recycled_sounds[--ctx->recycled_count];
    } else {
        if (ctx->sounds_allocated == MAX_SOUNDS) return (sound_id_t)0u;
        auto sound_index = ctx->sounds_allocated++;
        sound_id = sound_id_t(sound_index + 1);
    }

    sound_data_t* data_ptr = get_sound_data(ctx, sound_id);

    const sound_data_t null_data = {};
    *data_ptr = null_data;

    *out_sound_ptr = data_ptr;
    return sound_id;
}

static void release_sound(hlea_context_t* ctx, sound_id_t sound_id) {
    assert(ctx);
    assert(sound_id);

    assert(ctx->recycled_count < MAX_SOUNDS);
    ctx->recycled_sounds[ctx->recycled_count++] = sound_id;
}

static bank_streaming_source_info_t retrieve_bank_streaming_info(hlea_context_t* ctx, 
        hlea_event_bank_t* bank, uint32_t file_index) {
    
    auto buf_ptr = bank->data_buffer_ptr;
    if (bank->static_data->file_data.count && bank->streaming_afile) {
        auto& fd_ref = bank->static_data->file_data.get(buf_ptr, file_index);

        range_t range = {};
        range.offset = fd_ref.data_buffer.elements.pos;
        range.size = fd_ref.data_buffer.count;

        bank_streaming_source_info_t res = {};
        res.streaming_src = bank->streaming_cache_src;
        res.file_range = range;

        return res;
    }
#ifdef HLEA_USE_RT_EDITOR
    else if (ctx->editor_hooks) {
        // editor path, per file reading
        const char* path = bank->static_data->sound_files.get(buf_ptr, file_index).get_ptr(buf_ptr);
        return retrieve_streaming_info(ctx->editor_hooks, path, file_index);
    }
#endif

    return {};
}

static streaming_data_source_t* find_unused_streaming_source(hlea_context_t* ctx) {
    // todo: O(n), use freelist
    for (auto& src : ctx->streaming_sources) {
        if (src.channels == 0) {
            return &src;
        }
    }

    return nullptr;
}

struct decoder_result_t {
    decoder_t decoder;
    uint16_t dec_index;
};

static decoder_result_t acquire_decoder(hlea_context_t* ctx, audio_format_type_e audio_format) {
    using hle_audio::rt::mp3_decoder_t;

    decoder_result_t res = {};

    // todo: ugly and bulky per type decoder storage

    switch (audio_format) {
    case audio_format_type_e::mp3: {
        mp3_decoder_t* mp3_dec = {};
        if (!ctx->unused_decoders_mp3_indices.empty()) {
            auto recycled_index = ctx->unused_decoders_mp3_indices.pop_back();
            mp3_dec = ctx->decoders_mp3.vec[recycled_index];
            res.dec_index = recycled_index;

            reset(mp3_dec);
            
        } else {
            hle_audio::rt::mp3_decoder_create_info_t dec_init_info = {};
            dec_init_info.allocator = ctx->allocator;
            dec_init_info.jobs = ctx->jobs;
            mp3_dec = create_decoder(dec_init_info);

            res.dec_index = ctx->decoders_mp3.size;
            ctx->decoders_mp3.push_back(mp3_dec);
        }
        res.decoder = cast_to_decoder(mp3_dec);

        break;
    }
    default:
        assert(false && "not supported format");
        break;
    }

    return res;
}

static void release_decoder(hlea_context_t* ctx, audio_format_type_e audio_format, uint16_t dec_index) {
    switch (audio_format) {
    case audio_format_type_e::mp3: {
        ctx->unused_decoders_mp3_indices.push_back(dec_index);

        break;
    }
    default:
        assert(false && "not supported format");
        break;
    }
}

static sound_id_t make_sound(hlea_context_t* ctx, 
        hlea_event_bank_t* bank, uint8_t output_bus_index,
        const hle_audio::rt::file_node_t* file_node) {
    const sound_id_t invalid_id = (sound_id_t)0u;

    const ma_uint32 sound_flags = MA_SOUND_FLAG_NO_SPATIALIZATION;

    auto buf_ptr = bank->data_buffer_ptr;

    ma_decoder_config decoder_config = ma_decoder_config_init(
        ma_format_f32, 
        0, 
        ma_engine_get_sample_rate(&ctx->engine));
    decoder_config.allocationCallbacks = make_allocation_callbacks(&ctx->allocator);

    assert(bank->static_data->file_data.count);
    auto& fd_ref = bank->static_data->file_data.get(buf_ptr, file_node->file_index);
    file_data_t::meta_t meta = fd_ref.meta;

    data_buffer_t buffer_data = {};
    if (fd_ref.data_buffer.count) {
        buffer_data.data = (uint8_t*)fd_ref.data_buffer.elements.get_ptr(buf_ptr); // todo: void* cast, but read only here
        buffer_data.size = fd_ref.data_buffer.count;
    }

    sound_data_t* sound = nullptr;
    auto sound_id = acquire_sound(ctx, &sound);
    if (!sound_id) {
        // sound limit reached
        return invalid_id;
    }

    if (meta.stream) {
        streaming_data_source_t* str_src = find_unused_streaming_source(ctx);
        if (str_src) {
            auto streaming_info = retrieve_bank_streaming_info(ctx, bank, file_node->file_index);
            if (streaming_info.streaming_src) {
                auto dec_data = acquire_decoder(ctx, meta.coding_format);
                sound->decoder_internal = dec_data.decoder;
                sound->coding_format = meta.coding_format;
                sound->dec_index = dec_data.dec_index;

                streaming_data_source_init_info_t info = {};
                auto& dec_info = info.decoder_reader_info;

                dec_info.streaming_cache = ctx->streaming_cache;
                dec_info.input_src = streaming_info.streaming_src;
                dec_info.buffer_block = streaming_info.file_range;
                dec_info.decoder = dec_data.decoder;

                info.meta = meta;

                auto result = streaming_data_source_init(str_src, info);
                if (result == MA_SUCCESS) {
                    sound->str_src = str_src;

                    result = ma_sound_init_from_data_source(&ctx->engine, 
                        str_src,
                        sound_flags, 
                        &ctx->output_bus_groups[output_bus_index], 
                        &sound->engine_sound);

                    if (result == MA_SUCCESS) {
                        ma_sound_set_looping(&sound->engine_sound, file_node->loop);

                        return sound_id;
                    }

                    release_decoder(ctx, meta.coding_format, dec_data.dec_index);
                    streaming_data_source_uninit(str_src);
                } else {
                    release_decoder(ctx, meta.coding_format, dec_data.dec_index);
                }
            }
        }

    //
    // non-stream path
    //
    } else if (buffer_data.data) {
        // Init decoder
        auto result = ma_decoder_init_memory(buffer_data.data, buffer_data.size, &decoder_config, &sound->decoder);
        if (result == MA_SUCCESS) {
            // Init sound
            result = ma_sound_init_from_data_source(&ctx->engine, 
                    &sound->decoder,
                    sound_flags, 
                    &ctx->output_bus_groups[output_bus_index], 
                    &sound->engine_sound);

            if (result == MA_SUCCESS) {
                ma_sound_set_looping(&sound->engine_sound, file_node->loop);

                if (meta.loop_end) {
                    auto loop_start = ma_calculate_frame_count_after_resampling(ma_engine_get_sample_rate(&ctx->engine), meta.sample_rate, meta.loop_start);
                    auto loop_end = ma_calculate_frame_count_after_resampling(ma_engine_get_sample_rate(&ctx->engine), meta.sample_rate, meta.loop_end);

                    /**
                     * Internal decoder loop frames have a slight bias
                     * (1 frame at least) because of resampling calculation.
                     * todo: check if this is a problem
                     */
                    ma_data_source_set_loop_point_in_pcm_frames(&sound->decoder, loop_start, loop_end);
                }

                return sound_id;
            }

            ma_decoder_uninit(&sound->decoder);
        }
    }

    release_sound(ctx, sound_id);
    return invalid_id;
}

template<typename T>
constexpr memory_layout_t static_type_layout() {
    return {sizeof(T), alignof(T)};
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

static node_desc_t sequence_process_node(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* state) {
    auto s = (sequence_rt_state_t*)state;
    
    auto seq_node = bank_get(bank, bank->static_data->nodes_sequence, node_desc.index);

    auto seq_size = seq_node->nodes.count;
    if (seq_size <= s->current_index) return {};

    auto res = seq_node->nodes.get(bank->data_buffer_ptr, s->current_index);

    // update state
    ++s->current_index;

    return res;
}

static node_desc_t repeat_process_node(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* state) {
    auto s = (repeat_rt_state_t*)state;
        
    auto repeat_node = bank_get(bank, bank->static_data->nodes_repeat, node_desc.index);

    if (repeat_node->repeat_count && repeat_node->repeat_count <= s->iteration_counter) return {};

    auto res = repeat_node->node;

    // update state
    ++s->iteration_counter;

    return res;
}

static node_desc_t random_process_node(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* /*state*/) {
    auto random_node = bank_get(bank, bank->static_data->nodes_random, node_desc.index);
    int random_index = rand() % random_node->nodes.count;
    return random_node->nodes.get(bank->data_buffer_ptr, random_index);
}

struct node_funcs_t {
    memory_layout_t state_mem_layout;

    node_desc_t (*process_func)(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* state);
};

static const node_funcs_t g_node_handlers[] = {
    {}, // None
    {}, // File
    {{}, random_process_node}, // Random
    {static_type_layout<sequence_rt_state_t>(), sequence_process_node}, // Sequence
    {static_type_layout<repeat_rt_state_t>(), repeat_process_node}, // Repeat
};

static node_desc_t process_node(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* state) {
    auto process_func = g_node_handlers[size_t(node_desc.type)].process_func;
    if (process_func) {
        return process_func(bank, node_desc, state);
    }

    return {};
}

static bool init_and_push_state(node_state_stack_t& stack, const node_desc_t& node_desc) {
    memory_layout_t layout = g_node_handlers[size_t(node_desc.type)].state_mem_layout;
    if (layout.size) {
        push_state(stack, node_desc, layout);
        // init state memory, (implement init func for something fancy)

        return true;
    }
    return false;
}

static node_desc_t next_node_statefull(node_state_stack_t& stack, const hlea_event_bank_t* bank) {
    node_desc_t next_node_desc = {};
    while (next_node_desc.type == node_type_e::None && !is_empty(stack)) {
        next_node_desc = process_node(bank, top_node_desc(stack), top_state(stack));
        if (next_node_desc.type == node_type_e::None) pop_up_state(stack);
    }

    return next_node_desc;
}

static sound_id_t make_next_sound(hlea_context_t* ctx, group_data_t& group) {
    auto group_sdata = bank_get_group(group.bank, group.group_index);

    node_desc_t next_node_desc = {};
    
    if (is_empty(group.state_stack)) {
        // first run
        next_node_desc = group_sdata->node;
    } else {
        next_node_desc = next_node_statefull(group.state_stack, group.bank);
    }

    while(next_node_desc.type != node_type_e::None) {
        // sound node found, interupt traversing, generate sound
        if (next_node_desc.type == node_type_e::File) {
            auto file_node = bank_get(group.bank, group.bank->static_data->nodes_file, next_node_desc.index);
            return make_sound(ctx, group.bank, group_sdata->output_bus_index, file_node);
        }

        if (init_and_push_state(group.state_stack, next_node_desc)) {
            // use stacked node
            next_node_desc = next_node_statefull(group.state_stack, group.bank);
        } else {
            // stateless
            next_node_desc = process_node(group.bank, next_node_desc, nullptr);
        }
    }

    return invalid_sound_id;
}

static void start_next_after_current(hlea_context_t* ctx, group_data_t& group) {
    assert(group.sound_id);
    if (!group.next_sound_id) return;

    auto sound_data_ptr = get_sound_data(ctx, group.sound_id);
    auto sound = &sound_data_ptr->engine_sound;
    if (ma_sound_is_looping(sound)) return;
    
    auto engine_rate = ma_engine_get_sample_rate(&ctx->engine);

    ma_uint64 cursor, length;
    ma_sound_get_cursor_in_pcm_frames(sound, &cursor);
    ma_sound_get_length_in_pcm_frames(sound, &length);
    ma_uint32 sample_rate;
    ma_sound_get_data_format(sound, NULL, NULL, &sample_rate, NULL, 0);

    ma_uint64 rest_frames_local_freq = length - cursor;
    auto rest_frames_time = (double)rest_frames_local_freq / sample_rate;
    auto rest_frames_pcm = ma_uint64(rest_frames_time * engine_rate);

    auto sound_finished = ma_sound_at_end(sound);

    auto group_data = bank_get_group(group.bank, group.group_index);

    auto fade_time_pcm = (ma_uint64)(group_data->cross_fade_time * engine_rate);

    auto next_sound_data_ptr = get_sound_data(ctx, group.next_sound_id);
    auto next_sound = &next_sound_data_ptr->engine_sound;
    if (fade_time_pcm) {
        ma_sound_set_fade_in_pcm_frames(next_sound, -1, 1, fade_time_pcm);
    }
    if (!sound_finished && (group_data->cross_fade_time < rest_frames_time)) {
        ma_sound_set_start_time_in_pcm_frames(next_sound, ma_engine_get_time(&ctx->engine) + rest_frames_pcm - fade_time_pcm);
    }
    ma_sound_set_stop_time_in_pcm_frames(next_sound, (ma_uint64)-1);
    ma_sound_start(next_sound);

    if (!sound_finished) {
        group.apply_sound_fade_out = true;
    }
}

static void group_make_next_sound(hlea_context_t* ctx, group_data_t& group) {
    group.next_sound_id = invalid_sound_id;

    // no state left, leave
    if (is_empty(group.state_stack)) return;

    group.next_sound_id = make_next_sound(ctx, group);
    if (!group.next_sound_id) return;
    
    auto group_data = bank_get_group(group.bank, group.group_index);

    auto next_sound_data_ptr = get_sound_data(ctx, group.next_sound_id);
    ma_sound_set_volume(&next_sound_data_ptr->engine_sound, group_data->volume);

    start_next_after_current(ctx, group);
}

static void group_play(hlea_context_t* ctx, const event_desc_t* desc) {
    if (ctx->active_groups_size == MAX_ACTIVE_GROUPS) return;

    group_data_t group = {};
    group.bank = desc->bank;
    group.group_index = desc->target_index;
    group.obj_id = desc->obj_id;
    // todo: use pooled allocator instead of general one
    // todo: control size
    init(group.state_stack, 128, ctx->allocator);

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

static void release_sound_data(hlea_context_t* ctx, sound_id_t sound_id) {
    auto sound_data_ptr = get_sound_data(ctx, sound_id);

    if(sound_data_ptr->str_src) {
        release_decoder(ctx, sound_data_ptr->coding_format, sound_data_ptr->dec_index);

        streaming_data_source_uninit(sound_data_ptr->str_src);
        sound_data_ptr->str_src = nullptr;
    } else {
        ma_decoder_uninit(&sound_data_ptr->decoder);
    }

    release_sound(ctx, sound_id);
}

static void uninit_and_release_sound(hlea_context_t* ctx, sound_id_t sound_id) {
    auto sound_data_ptr = get_sound_data(ctx, sound_id);
    
    ma_sound_uninit(&sound_data_ptr->engine_sound);

    // defer uninit if streaming source is not ready
    if(sound_data_ptr->str_src && is_running(sound_data_ptr->decoder_internal)) {
        assert(ctx->pending_streaming_sounds_size < (sizeof(ctx->pending_streaming_sounds) / sizeof(ctx->pending_streaming_sounds[0])));
        ctx->pending_streaming_sounds[ctx->pending_streaming_sounds_size++] = sound_id;
        return;
    }

    release_sound_data(ctx, sound_id);
}

static void group_active_release(hlea_context_t* ctx, uint32_t active_index) {
    group_data_t& group = ctx->active_groups[active_index];

    // clean up state data
    deinit(group.state_stack);

    // swap remove
    ctx->active_groups[active_index] = ctx->active_groups[ctx->active_groups_size - 1];
    --ctx->active_groups_size;
}

static ma_result task_executor_job_process(ma_job* pJob) {
    hlea_job_t job = {};
    job.job_func = (decltype(job.job_func))pJob->data.custom.data0;
    job.udata = (decltype(job.udata))pJob->data.custom.data1;

    job.job_func(job.udata);

    return MA_SUCCESS;
}

static void task_executor_launch(void* udata, hlea_job_t job) {
    auto executor = (ma_device_job_thread*)udata;

    auto ma_job = ma_job_init(MA_JOB_TYPE_CUSTOM);
    ma_job.data.custom.proc = task_executor_job_process;
    ma_job.data.custom.data0 = (ma_uintptr)job.job_func;
    ma_job.data.custom.data1 = (ma_uintptr)job.udata;
    auto result = ma_device_job_thread_post(executor, &ma_job);
}

static const hlea_jobs_ti s_task_executor_jobs_vt {
    task_executor_launch
};

hlea_context_t* hlea_create(hlea_context_create_info_t* info) {

    allocator_t alloc = hle_audio::make_default_allocator();
    if (info->allocator_vt) {
        alloc = allocator_t{info->allocator_vt, info->allocator_udata};
    }

    auto ctx = allocate_unique<hlea_context_t>(alloc);
    memset(ctx.get(), 0, sizeof(hlea_context_t));
    ctx->allocator = alloc;

    auto allocation_callbacks = make_allocation_callbacks(&ctx->allocator);

    // setup runtime common vfs
    ma_default_vfs_init(&ctx->vfs_default, &allocation_callbacks);
    ctx->pVFS = &ctx->vfs_default;

    auto config = ma_engine_config_init();
    if (info->file_api_vt) {
        init(ctx->vfs_impl, info->file_api_vt, info->file_sys);
        ctx->pVFS = &ctx->vfs_impl;
    }
    config.allocationCallbacks = allocation_callbacks;

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
    
    if (info->jobs_vt) {
        jobs_t jobs_impl = {};
        jobs_impl.vt = info->jobs_vt;
        jobs_impl.udata = info->jobs_udata;
        
        ctx->jobs = jobs_impl;
    } else {
        // init single thread pool as default job processor
        auto jobThreadConfig = ma_device_job_thread_config_init();
        ma_device_job_thread_init(&jobThreadConfig, &config.allocationCallbacks, &ctx->task_executor);

        jobs_t jobs_impl = {};
        jobs_impl.vt = &s_task_executor_jobs_vt;
        jobs_impl.udata = &ctx->task_executor;
        
        ctx->jobs = jobs_impl;
    }

    hle_audio::rt::async_file_reader_create_info_t cinfo = {};
    cinfo.allocator = ctx->allocator;
    cinfo.vfs = ctx->pVFS;
    ctx->async_io = hle_audio::rt::create_async_file_reader(cinfo);

    hle_audio::rt::chunk_streaming_cache_init_info_t cache_iinfo = {};
    cache_iinfo.allocator = ctx->allocator;
    cache_iinfo.async_io = ctx->async_io;
    ctx->streaming_cache = hle_audio::rt::create_cache(cache_iinfo);

    return ctx.release();
}

#ifdef HLEA_USE_RT_EDITOR
void hlea_bind(hlea_context_t* ctx, editor_runtime_t* editor_rt) {
    ctx->editor_hooks = editor_rt;

    hle_audio::rt::runtime_env_t env = {};
    env.pVFS = ctx->pVFS;
    env.allocator = ctx->allocator;
    env.async_io = ctx->async_io;
    env.cache = ctx->streaming_cache;
    env.engine = &ctx->engine;
    bind(editor_rt, &env);
}
#endif

void hlea_destroy(hlea_context_t* ctx) {
    destroy(ctx->streaming_cache);
    destroy(ctx->async_io);

    if (ctx->jobs.vt == &s_task_executor_jobs_vt) {
        auto alloc_cb = make_allocation_callbacks(&ctx->allocator);
        ma_device_job_thread_uninit(&ctx->task_executor, &alloc_cb);
    }

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
    if (data_header->version != hle_audio::rt::STORE_BLOB_VERSION) {
        deallocate(ctx->allocator, pData);
        return nullptr;
    }

    buffer_t buf = {};
    buf.ptr = pData;

    auto store = data_header->store.get_ptr(buf);

    // todo: small allocation
    auto bank = allocate<hlea_event_bank_t>(ctx->allocator);

    *bank = {};
    bank->data_buffer_ptr = buf;
    bank->static_data = store;

    return bank;
}

hlea_event_bank_t* hlea_load_events_bank(hlea_context_t* ctx, const char* bank_filename, const char* stream_bank_filename) {
    data_buffer_t buffer = {};
    ma_result result = read_file(ctx->pVFS, bank_filename, ctx->allocator, &buffer);

    hlea_event_bank_t* res = load_events_bank_buffer(ctx, buffer.data);

    result = ma_vfs_open(ctx->pVFS, stream_bank_filename, MA_OPEN_MODE_READ, &res->streaming_file);
    if (result == MA_SUCCESS) {
        res->streaming_afile = start_async_reading(ctx->async_io, res->streaming_file);
        res->streaming_cache_src = register_source(ctx->streaming_cache, res->streaming_afile);
    } else {
        // couldn't open file, do nothing here
    } 

    return res;
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

    if (bank->streaming_afile) {
        // safe as all sounds're stopped feeding from the stream
        deregister_source(ctx->streaming_cache, bank->streaming_cache_src);
        bank->streaming_cache_src = {};

        // wait for all reads to finish and stop
        stop_async_reading(ctx->async_io, bank->streaming_afile);
        bank->streaming_afile = {};

        // no more pending reads, close the file
        ma_vfs_close(ctx->pVFS, bank->streaming_file);
        bank->streaming_file = {};
    }

    // todo: push decoder could be using data_buffer_ptr (not yet the case), so need to keep buffer until 
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

                ma_uint32 sample_rate;
                ma_sound_get_data_format(&sound_data_ptr->engine_sound, NULL, NULL, &sample_rate, NULL, 0);

                auto engine_rate = ma_engine_get_sample_rate(&ctx->engine);

                ma_uint64 rest_frames_local_freq = length - cursor;
                auto rest_frames_time = (double)rest_frames_local_freq / sample_rate;
                auto rest_frames_pcm = ma_uint64(rest_frames_time * engine_rate);

                auto group_data = bank_get_group(group.bank, group.group_index);

                if (rest_frames_time <= group_data->cross_fade_time) {
                    group.apply_sound_fade_out = false;
                    ma_sound_set_fade_in_pcm_frames(&sound_data_ptr->engine_sound, -1, 0, rest_frames_pcm);
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

static void process_pending_streaming_sounds(hlea_context_t* ctx) {
    for (size_t i = 0; i < ctx->pending_streaming_sounds_size; ++i) {
        auto sound_id = ctx->pending_streaming_sounds[i];

        auto sound_data_ptr = get_sound_data(ctx, sound_id);

        assert(sound_data_ptr->str_src);
        if (!is_running(sound_data_ptr->decoder_internal)) {
            //
            // finish streaming deinit of uninit_and_release_sound
            //
            release_sound_data(ctx, sound_id);

            // swap remove
            ctx->pending_streaming_sounds[i] = ctx->pending_streaming_sounds[--ctx->pending_streaming_sounds_size];
            --i;
        }
    }
}

void hlea_process_frame(hlea_context_t* ctx) {
    hlea_process_active_groups(ctx);
    process_pending_streaming_sounds(ctx);
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

/**************************************************************************************************
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

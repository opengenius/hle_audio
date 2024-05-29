#include "hlea/runtime.h"
#include "miniaudio_public.h"
#include "internal_types.h"
#include "internal_editor_runtime.h"

using hle_audio::rt::node_state_stack_t;
using hle_audio::rt::named_group_t;
using hle_audio::rt::node_desc_t;
using hle_audio::rt::data_buffer_t;
using hle_audio::rt::file_data_t;
using hle_audio::rt::node_type_e;
using hle_audio::rt::streaming_data_source_t;
using hle_audio::rt::streaming_data_source_init_info_t;
using hle_audio::rt::buffer_data_source_t;
using hle_audio::rt::buffer_data_source_init_info_t;
using hle_audio::rt::range_t;
using hle_audio::rt::audio_format_type_e;
using hle_audio::rt::decoder_t;

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

static streaming_data_source_t* acquire_streaming_data_source(hlea_context_t* ctx) {
    if (!ctx->unused_streaming_sources_indices.empty()) {
        auto source_index = ctx->unused_streaming_sources_indices.pop_back();
        return &ctx->streaming_sources.vec[source_index];
    } else if (!ctx->streaming_sources.is_full()) {
        ctx->streaming_sources.push_back({});
        return &ctx->streaming_sources.last();
    }

    return nullptr;
}

static void release_streaming_data_source(hlea_context_t* ctx, streaming_data_source_t* src) {
    auto index = src - ctx->streaming_sources.vec;
    ctx->unused_streaming_sources_indices.push_back(index);
}

static buffer_data_source_t* acquire_buffer_data_source(hlea_context_t* ctx) {
    if (!ctx->unused_buffer_sources_indices.empty()) {
        auto buffer_source_index = ctx->unused_buffer_sources_indices.pop_back();
        return &ctx->buffer_sources.vec[buffer_source_index];
    } else if (!ctx->buffer_sources.is_full()) {
        ctx->buffer_sources.push_back({});
        return &ctx->buffer_sources.last();
    }

    return nullptr;
}

static void release_buffer_data_source(hlea_context_t* ctx, buffer_data_source_t* src) {
    auto index = src - ctx->buffer_sources.vec;
    ctx->unused_buffer_sources_indices.push_back(index);
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
        return retrieve_streaming_info(ctx->editor_hooks, file_index);
    }
#endif

    return {};
}

struct decoder_result_t {
    ma_format format;
    decoder_t decoder;
    uint16_t dec_index;
};

static decoder_result_t acquire_decoder(hlea_context_t* ctx, audio_format_type_e audio_format) {
    using hle_audio::rt::mp3_decoder_t;
    using hle_audio::rt::pcm_decoder_t;

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
        res.format = ma_format_f32;

        break;
    }
    case audio_format_type_e::pcm: {
        pcm_decoder_t* dec_inst = {};
        if (!ctx->unused_decoders_pcm_indices.empty()) {
            auto recycled_index = ctx->unused_decoders_pcm_indices.pop_back();
            dec_inst = ctx->decoders_pcm.vec[recycled_index];
            res.dec_index = recycled_index;

            reset(dec_inst);
            
        } else {
            hle_audio::rt::pcm_decoder_create_info_t dec_init_info = {};
            dec_init_info.allocator = ctx->allocator;
            // dec_init_info.jobs = ctx->jobs;
            dec_inst = create_decoder(dec_init_info);

            res.dec_index = ctx->decoders_pcm.size;
            ctx->decoders_pcm.push_back(dec_inst);
        }
        res.decoder = cast_to_decoder(dec_inst);
        res.format = ma_format_s16;

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
    case audio_format_type_e::pcm: {
        ctx->unused_decoders_pcm_indices.push_back(dec_index);

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

    auto dec_data = acquire_decoder(ctx, meta.coding_format);
    sound->decoder = dec_data.decoder;
    sound->coding_format = meta.coding_format;
    sound->dec_index = dec_data.dec_index;

    if (meta.stream) {
        streaming_data_source_t* str_src = acquire_streaming_data_source(ctx);
        if (str_src) {
            auto streaming_info = retrieve_bank_streaming_info(ctx, bank, file_node->file_index);
            if (streaming_info.streaming_src) {
                streaming_data_source_init_info_t info = {};
                auto& dec_info = info.decoder_reader_info;

                dec_info.streaming_cache = ctx->streaming_cache;
                dec_info.input_src = streaming_info.streaming_src;
                dec_info.buffer_block = streaming_info.file_range;
                dec_info.decoder = dec_data.decoder;

                info.format = dec_data.format;
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

                    streaming_data_source_uninit(str_src);   
                }
            }
            release_streaming_data_source(ctx, str_src);
        }

    //
    // non-stream path
    //
    } else if (buffer_data.data) {

        auto src = acquire_buffer_data_source(ctx);
        if (src) {
            buffer_data_source_init_info_t info = {};
            info.decoder = dec_data.decoder;
            info.format = dec_data.format;
            info.meta = meta;
            info.buffer = buffer_data;
            auto result = buffer_data_source_init(src, info);
            if (result == MA_SUCCESS) {
                sound->buffer_src = src;

                // setup loop point
                if (meta.loop_end) {
                    ma_data_source_set_loop_point_in_pcm_frames(src, meta.loop_start, meta.loop_end);
                }

                // Init sound
                result = ma_sound_init_from_data_source(&ctx->engine, 
                        src,
                        sound_flags, 
                        &ctx->output_bus_groups[output_bus_index], 
                        &sound->engine_sound);
                if (result == MA_SUCCESS) {
                    ma_sound_set_looping(&sound->engine_sound, file_node->loop);
                
                    return sound_id;
                }

                buffer_data_source_uninit(src);
            }

            release_buffer_data_source(ctx, src);
        }
    }

    release_decoder(ctx, meta.coding_format, dec_data.dec_index);

    release_sound(ctx, sound_id);
    return invalid_id;
}

static const named_group_t* bank_get_group(const hlea_event_bank_t* bank, uint32_t group_index) {
    return bank_get(bank, bank->static_data->groups, group_index);
}


static node_desc_t process_node(const hlea_event_bank_t* bank, const node_desc_t& node_desc, void* state) {
    auto process_func = get_node_handler(node_desc.type).process_func;
    if (process_func) {
        return process_func(bank, node_desc, state);
    }

    return {};
}

static bool init_and_push_state(node_state_stack_t& stack, const node_desc_t& node_desc) {
    memory_layout_t layout = get_node_handler(node_desc.type).state_mem_layout;
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

void fire_event(hlea_context_t* ctx, hlea_action_type_e event_type, const event_desc_t* desc) {
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

static void group_active_release(hlea_context_t* ctx, uint32_t active_index) {
    group_data_t& group = ctx->active_groups[active_index];

    // clean up state data
    deinit(group.state_stack);

    // swap remove
    ctx->active_groups[active_index] = ctx->active_groups[ctx->active_groups_size - 1];
    --ctx->active_groups_size;
}

static void release_sound_data(hlea_context_t* ctx, sound_id_t sound_id) {
    auto sound_data_ptr = get_sound_data(ctx, sound_id);

    if(sound_data_ptr->str_src) {
        streaming_data_source_uninit(sound_data_ptr->str_src);
        release_streaming_data_source(ctx, sound_data_ptr->str_src);
        sound_data_ptr->str_src = nullptr;

    } else if (sound_data_ptr->buffer_src) {
        buffer_data_source_uninit(sound_data_ptr->buffer_src);
        release_buffer_data_source(ctx, sound_data_ptr->buffer_src);
        sound_data_ptr->buffer_src = nullptr;
    }

    release_decoder(ctx, sound_data_ptr->coding_format, sound_data_ptr->dec_index);

    release_sound(ctx, sound_id);
}

static void uninit_and_release_sound(hlea_context_t* ctx, sound_id_t sound_id) {
    auto sound_data_ptr = get_sound_data(ctx, sound_id);
    
    ma_sound_uninit(&sound_data_ptr->engine_sound);

    // defer uninit if decoder is not finished
    if(is_running(sound_data_ptr->decoder)) {
        assert(ctx->pending_sounds_size < (sizeof(ctx->pending_sounds) / sizeof(ctx->pending_sounds[0])));
        ctx->pending_sounds[ctx->pending_sounds_size++] = sound_id;
        return;
    }

    release_sound_data(ctx, sound_id);
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

void process_pending_sounds(hlea_context_t* ctx) {
    for (size_t i = 0; i < ctx->pending_sounds_size; ++i) {
        auto sound_id = ctx->pending_sounds[i];

        auto sound_data_ptr = get_sound_data(ctx, sound_id);

        if (!is_running(sound_data_ptr->decoder)) {
            //
            // finish deinit of uninit_and_release_sound
            //
            release_sound_data(ctx, sound_id);

            // swap remove
            ctx->pending_sounds[i] = ctx->pending_sounds[--ctx->pending_sounds_size];
            --i;
        }
    }
}

void group_release_all_in_bank(hlea_context_t* ctx, const hlea_event_bank_t* bank) {
    for (uint32_t active_index = 0u; active_index < ctx->active_groups_size; ++active_index) {
        group_data_t& group = ctx->active_groups[active_index];
        if (group.bank != bank) continue;

        if (group.sound_id) uninit_and_release_sound(ctx, group.sound_id);
        if (group.next_sound_id) uninit_and_release_sound(ctx, group.next_sound_id);
        group_active_release(ctx, active_index);
        --active_index;
    }
}

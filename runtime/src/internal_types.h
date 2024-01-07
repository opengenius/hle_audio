#pragma once

#include <cstdint>
#include "rt_types.h"
#include "streaming_data_source.h"
#include "node_state_stack.h"
#include "chunk_streaming_cache.h"
#include "decoder_mp3.h"

static const uint16_t MAX_SOUNDS = 1024;
static const uint16_t MAX_ACTIVE_GROUPS = 128;
static const uint16_t SOUNDS_UNUSED_LIST = 0u;
static const uint8_t MAX_OUPUT_BUSES = 32u;
static const uint16_t MAX_STREAMING_SOURCES = MAX_SOUNDS;

enum sound_id_t : uint16_t;
const sound_id_t invalid_sound_id = (sound_id_t)0u;

struct sound_data_t {
    ma_decoder    decoder;
    hle_audio::rt::decoder_t decoder_internal;
    hle_audio::rt::audio_format_type_e coding_format;
    uint16_t dec_index;

    hle_audio::rt::streaming_data_source_t* str_src;
    ma_sound      engine_sound;
};

struct hlea_event_bank_t {
    hle_audio::rt::buffer_t data_buffer_ptr;
    const hle_audio::rt::store_t* static_data;
    ma_vfs_file streaming_file;
    hle_audio::rt::async_file_handle_t streaming_afile;
    hle_audio::rt::streaming_source_handle streaming_cache_src;
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

    hle_audio::rt::node_state_stack_t state_stack;
};

struct event_desc_t {
    hlea_event_bank_t* bank;
    uint32_t target_index;
    uint32_t obj_id;
    float fade_time;
};

template<typename T, size_t ARRAY_SIZE, typename CountType>
struct array_with_size_t {
    static_assert(ARRAY_SIZE <= std::numeric_limits<CountType>::max(), "CountType is not enough to store ARRAY_SIZE");

    T vec[ARRAY_SIZE];
    CountType size;

    bool empty() const {
        return size == 0;
    }

    void push_back(const T& v) {
        assert(size < ARRAY_SIZE);
        vec[size++] = v;
    }

    T pop_back() {
        assert(0 < size);
        return vec[--size];
    }
};

struct hlea_context_t {
    vfs_bridge_t vfs_impl;
    ma_default_vfs vfs_default;

    ma_device_job_thread task_executor;

    ma_vfs* pVFS;
    allocator_t allocator;
    jobs_t jobs;
    hle_audio::rt::async_file_reader_t* async_io;
    hle_audio::rt::chunk_streaming_cache_t* streaming_cache;

    ma_engine engine;

#ifdef HLEA_USE_RT_EDITOR
    hle_audio::rt::editor_runtime_t* editor_hooks;
#endif

    ma_sound_group output_bus_groups[MAX_OUPUT_BUSES];
    uint8_t output_bus_group_count;
    static_assert(sizeof(output_bus_group_count) < MAX_OUPUT_BUSES, "");

    sound_data_t sounds[MAX_SOUNDS];
    uint16_t sounds_allocated;
    
    sound_id_t recycled_sounds[MAX_SOUNDS];
    uint16_t recycled_count;

    // todo: use recycled_sounds tail block for pending
    sound_id_t pending_streaming_sounds[MAX_SOUNDS];
    uint16_t pending_streaming_sounds_size;

    group_data_t active_groups[MAX_ACTIVE_GROUPS];
    uint16_t active_groups_size;

    hle_audio::rt::streaming_data_source_t streaming_sources[MAX_STREAMING_SOURCES];

    array_with_size_t<hle_audio::rt::mp3_decoder_t*, MAX_SOUNDS, uint16_t> decoders_mp3;
    array_with_size_t<uint16_t, MAX_SOUNDS, uint16_t> unused_decoders_mp3_indices;
};

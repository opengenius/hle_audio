#pragma once

#include <cstdint>
#include <limits>
#include "rt_types.h"
#include "data_sources/streaming_data_source.h"
#include "data_sources/buffer_data_source.h"
#include "chunk_streaming_cache.h"
#include "internal_jobs_types.h"
#include "file_api_vfs_bridge.h"

namespace hle_audio { namespace rt {
struct editor_runtime_t;
struct mp3_decoder_t;
struct pcm_decoder_t;
}}

static const uint16_t MAX_SOUNDS = 1024;
static const uint16_t MAX_ACTIVE_GROUPS = 128;
static const uint16_t SOUNDS_UNUSED_LIST = 0u;
static const uint8_t MAX_OUPUT_BUSES = 32u;
static const uint16_t MAX_STREAMING_SOURCES = MAX_SOUNDS;

enum sound_id_t : uint16_t;
const sound_id_t invalid_sound_id = (sound_id_t)0u;

struct sound_data_t {
    using decoder_t = hle_audio::rt::decoder_t;
    using audio_format_type_e = hle_audio::rt::audio_format_type_e;
    using streaming_data_source_t = hle_audio::rt::streaming_data_source_t;
    using buffer_data_source_t = hle_audio::rt::buffer_data_source_t;

    decoder_t decoder;
    audio_format_type_e coding_format;
    uint16_t dec_index;

    streaming_data_source_t* str_src;
    buffer_data_source_t* buffer_src;
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

    hle_audio::rt::offset_t current_node_offset;
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

    bool is_full() const {
        return size == ARRAY_SIZE;
    }

    bool empty() const {
        return size == 0;
    }

    void push_back(const T& v) {
        assert(size < ARRAY_SIZE);
        vec[size++] = v;
    }

    T& last() {
        assert(size);
        return vec[size - 1];
    }

    T pop_back() {
        assert(0 < size);
        return vec[--size];
    }
};

template<typename T>
static const T* bank_get(const hlea_event_bank_t* bank, 
        const hle_audio::rt::array_view_t<T> arr, size_t index) {
    auto data_ptr = bank->data_buffer_ptr;
    return &arr.get(data_ptr, index);
}

struct bank_streaming_source_info_t {
    using streaming_source_handle = hle_audio::rt::streaming_source_handle;
    using range_t = hle_audio::rt::range_t;
    
    streaming_source_handle streaming_src;
    range_t file_range;
};

struct editor_api_t {
    using editor_runtime_t = hle_audio::rt::editor_runtime_t;

    editor_runtime_t* inst;
    bank_streaming_source_info_t (*retrieve_streaming_info)(editor_runtime_t* editor_rt, uint32_t file_index);
};

static bank_streaming_source_info_t retrieve_streaming_info(editor_api_t& editor_api, uint32_t file_index) {
    if (!editor_api.inst) return {};

    return editor_api.retrieve_streaming_info(editor_api.inst, file_index);
}

struct hlea_context_t {
    using streaming_data_source_t = hle_audio::rt::streaming_data_source_t;
    using buffer_data_source_t = hle_audio::rt::buffer_data_source_t;

    vfs_bridge_t vfs_impl;
    ma_default_vfs vfs_default;

    ma_device_job_thread task_executor;

    ma_vfs* pVFS;
    allocator_t allocator;
    jobs_t jobs;
    hle_audio::rt::async_file_reader_t* async_io;
    hle_audio::rt::chunk_streaming_cache_t* streaming_cache;

    ma_engine engine;

    editor_api_t editor_hooks;

    ma_sound_group output_bus_groups[MAX_OUPUT_BUSES];
    uint8_t output_bus_group_count;
    static_assert(sizeof(output_bus_group_count) < MAX_OUPUT_BUSES, "");

    sound_data_t sounds[MAX_SOUNDS];
    uint16_t sounds_allocated;
    
    sound_id_t recycled_sounds[MAX_SOUNDS];
    uint16_t recycled_count;

    // todo: use recycled_sounds tail block for pending
    sound_id_t pending_sounds[MAX_SOUNDS];
    uint16_t pending_sounds_size;

    group_data_t active_groups[MAX_ACTIVE_GROUPS];
    uint16_t active_groups_size;

    array_with_size_t<streaming_data_source_t, MAX_STREAMING_SOURCES, uint16_t> streaming_sources;
    array_with_size_t<uint16_t, MAX_STREAMING_SOURCES, uint16_t> unused_streaming_sources_indices;

    array_with_size_t<buffer_data_source_t, MAX_SOUNDS, uint16_t> buffer_sources;
    array_with_size_t<uint16_t, MAX_SOUNDS, uint16_t> unused_buffer_sources_indices;

    array_with_size_t<hle_audio::rt::mp3_decoder_t*, MAX_SOUNDS, uint16_t> decoders_mp3;
    array_with_size_t<uint16_t, MAX_SOUNDS, uint16_t> unused_decoders_mp3_indices;

    array_with_size_t<hle_audio::rt::pcm_decoder_t*, MAX_SOUNDS, uint16_t> decoders_pcm;
    array_with_size_t<uint16_t, MAX_SOUNDS, uint16_t> unused_decoders_pcm_indices;
};


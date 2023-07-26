#pragma once

#include <cstdint>
#include <cassert>

namespace hle_audio {
namespace rt {

//
// base blob types
//

typedef uint32_t offset_t;

struct buffer_t {
    void* ptr;    
};

struct file_buffer_data_t {
    void* data;
    size_t size;
};

template<typename T>
struct offset_typed_t {
    offset_t pos;

    const T* get_ptr(const buffer_t& buf) const {
        return reinterpret_cast<T*>(&static_cast<uint8_t*>(buf.ptr)[pos]);
    }
};

using char_offset_t = offset_typed_t<char>;

template<typename T>
struct array_view_t {
    uint32_t count;
    offset_typed_t<T> elements;

    const T& get(const buffer_t& buf, size_t index) const {
        assert(index < count);
        return elements.get_ptr(buf)[index];
    }
};

//
// rt blob types
//

static const uint32_t STORE_BLOB_VERSION = 2;

enum class node_type_e : uint8_t {
    None,
    File,
    Random,
    Sequence,
    Repeat
};

static const char* const c_node_type_names[] = {
    "None",
    "File",
    "Random",
    "Sequence",
    "Repeat"
};

static const char* node_type_name(node_type_e type) {
    return c_node_type_names[(size_t)type];
}

struct file_node_t {
    uint32_t file_index;
    bool loop;
    bool stream;
};

struct node_desc_t {
    node_type_e type;
    uint16_t index;
};

struct random_node_t {
    array_view_t<node_desc_t> nodes;
};

struct sequence_node_t {
    array_view_t<node_desc_t> nodes;
};

struct repeat_node_t {
    uint16_t repeat_count;
    node_desc_t node;
};

struct named_group_t {
    char_offset_t name;
    float volume = 1.0;
    float cross_fade_time = 0.0;
    uint8_t output_bus_index = 0;
    node_desc_t node;
};

enum class action_type_e : uint8_t {
    none,
    play_single,
    play,
    stop,
    break_loop,
    pause,
    resume,
    pause_bus,
    resume_bus,
    stop_bus,
    stop_all
};

static const char* const c_action_type_names[] = {
    "none",
    "play_single",
    "play",
    "stop",
    "break_loop",
    "pause",
    "resume",
    "pause_bus",
    "resume_bus",
    "stop_bus",
    "stop_all"
};

static const char* action_type_name(action_type_e type) {
    return c_action_type_names[(size_t)type];
}

struct action_t {
    action_type_e type;
    uint32_t target_index;
    float fade_time;
};

struct event_t {
    char_offset_t name;
    array_view_t<action_t> actions;
};

struct file_data_t {
    struct meta_t {
        uint64_t loop_start;
        uint64_t loop_end;
        // ? +SampleRate
    };

    meta_t meta;
    array_view_t<uint8_t> data_buffer;
};

struct store_t {
    array_view_t<char_offset_t> sound_files;
    array_view_t<file_node_t> nodes_file;
    array_view_t<random_node_t> nodes_random;
    array_view_t<sequence_node_t> nodes_sequence;
    array_view_t<repeat_node_t> nodes_repeat;

    array_view_t<named_group_t> groups;
    array_view_t<event_t> events;

    array_view_t<file_data_t> file_data;
};

struct root_header_t {
    uint32_t version = STORE_BLOB_VERSION;
    offset_typed_t<store_t> store;
};

}
}

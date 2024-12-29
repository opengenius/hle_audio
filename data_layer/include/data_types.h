#pragma once

#include "sparse_vector.h"
#include "index_id.h"
#include <string>
#include "rt_types.h"

namespace hle_audio {
namespace data {

enum node_id_t : uint32_t;

struct event_t {
    std::string name{};
    std::vector<rt::action_t> actions;
};

struct output_bus_t {
    std::string name;
};

enum flow_node_type_t : uint8_t {
    INVALID = 0,
    FILE_FNODE_TYPE,
    RANDOM_FNODE_TYPE,
    FADE_FNODE_TYPE,
    DELAY_FNODE_TYPE
};

const size_t invalid_index = ~0;
const auto invalid_file_index = (uint16_t)~0u;
const node_id_t invalid_node_id = {};
const flow_node_type_t invalid_node_type = flow_node_type_t::INVALID;

static const char* c_flow_node_type_names[] = {
    "",
    "File",
    "Random",
    "Fade",
    "Delay"
};
static const char* flow_node_type_name(flow_node_type_t type) {
    assert(uint8_t(type) < std::size(c_flow_node_type_names));
    return c_flow_node_type_names[uint8_t(type)];
}
static flow_node_type_t flow_node_type_from_str(const char* str) {
    int i = 0;
    for (auto name : c_flow_node_type_names) {
        if (strcmp(name, str) == 0) {
            return (flow_node_type_t)i;
        }
        ++i;
    }
    return invalid_node_type;
}

struct file_flow_node_t {
    static const uint16_t NEXT_NODE_OUT_PIN = 0;
    static const uint16_t FILTER_OUT_PIN = 1;

    std::u8string filename;
    bool loop = false;
    bool stream = false;
};

struct random_flow_node_t {
    uint16_t out_pin_count = 2;
};

struct fade_flow_node_t {
    float start_time;
    float end_time;
};

struct delay_flow_node_t {
    static const uint16_t NEXT_NODE_OUT_PIN = 0;
    
    float time;
};

struct attribute_t {
    node_id_t node;
    uint16_t pin_index;
};

struct link_t {
    attribute_t from;
    attribute_t to;
};

enum class link_type_e : uint8_t {
    EXECUTION,
    FILTER
};

struct named_group_t {
    std::string name = {};
    float volume = 1.0f;
    uint8_t output_bus_index = 0;
    node_id_t start_node;
    std::vector<node_id_t> nodes;
    std::vector<link_t> links;
};

struct vec2_t {
    int16_t x;
    int16_t y;
};

struct common_flow_node_t {
    vec2_t position;

    flow_node_type_t type;
    uint32_t index;
};

struct pin_counts_t {
    uint16_t in_count;
    uint16_t out_count;
};

struct data_state_t {
    std::vector<named_group_t> groups;
    std::vector<event_t> events;

    utils::sparse_vector<common_flow_node_t> fnodes;
    utils::sparse_vector<file_flow_node_t> fnodes_file;
    utils::sparse_vector<random_flow_node_t> fnodes_random;
    utils::sparse_vector<fade_flow_node_t> fnodes_fade;
    utils::sparse_vector<delay_flow_node_t> fnodes_delay;

    std::vector<output_bus_t> output_buses;
};

static bool is_action_type_target_bus(const rt::action_type_e& type) {
    return type == rt::action_type_e::stop_bus ||
        type == rt::action_type_e::pause_bus ||
        type == rt::action_type_e::resume_bus;
}

static bool is_action_target_group(const rt::action_type_e& action_type) {
    return action_type != rt::action_type_e::none &&
        action_type != rt::action_type_e::stop_all &&
        !is_action_type_target_bus(action_type);
}

static bool is_event_target_group(const event_t& event, size_t group_index) {
    for (auto& action : event.actions) {
        if (!is_action_target_group(action.type)) continue;

        if (group_index == action.target_index) {
            return true;
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////

struct audio_file_data_t {
    rt::file_data_t::meta_t meta;
    std::vector<uint8_t> content;
    rt::range_t data_chunk_range;
};

class audio_file_data_provider_ti {
public:
    virtual audio_file_data_t get_file_data(const char* filename, uint32_t file_index) = 0;
};

std::vector<uint8_t> save_store_blob_buffer(const data_state_t* state, audio_file_data_provider_ti* fdata_provider, const char* streaming_filename = nullptr);

/**
 * @brief Init data state from Json file
 * 
 * @param state 
 * @param json_filename 
 * @return true if loaded successfully
 * @return false otherwise
 */
bool load_store_json(data_state_t* state, const char* json_filename);
void save_store_json(const data_state_t* state, const char* json_filename);

}
}

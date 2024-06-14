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

enum flow_node_type_t : uint8_t;

const size_t invalid_index = ~0;
const auto invalid_file_index = (uint16_t)~0u;
const node_id_t invalid_node_id = {};
const flow_node_type_t invalid_node_type = {};

static const flow_node_type_t FILE_FNODE_TYPE = flow_node_type_t(1);
static const flow_node_type_t RANDOM_FNODE_TYPE = flow_node_type_t(2);

static const char* c_flow_node_type_names[] = {
    "",
    "File",
    "Random"
};
static const char* flow_node_type_name(flow_node_type_t type) {
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
    std::u8string filename;
    bool loop = false;
    bool stream = false;
};

struct random_flow_node_t {
    uint16_t out_pin_count;
};

struct link_t {
    node_id_t from;
    uint16_t from_pin;
    node_id_t to;
    uint16_t to_pin;
};

struct named_group_t {
    std::string name = {};
    float volume = 1.0f;
    float cross_fade_time = 0.0f;
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

#pragma once

#include "sparse_vector.h"
#include "index_id.h"
#include <string>
#include "rt_types.h"

namespace hle_audio {
namespace editor {

struct node_desc_t {
    rt::node_type_e type;
    utils::index_id_t id;
};

struct file_node_t {
    std::u8string filename;
    bool loop = false;
    bool stream = false;
};

struct node_random_t {
    std::vector<node_desc_t> nodes;
};

struct node_sequence_t {
    std::vector<node_desc_t> nodes;
};

struct node_repeat_t {
    uint16_t repeat_count;
    node_desc_t node;
};

struct named_group_t {
    std::string name = {};
    float volume = 1.0f;
    float cross_fade_time = 0.0f;
    uint8_t output_bus_index = 0;
    node_desc_t node = {};
};

struct event_t {
    std::string name{};
    std::vector<rt::action_t> actions;
};

struct output_bus_t {
    std::string name;
};

const size_t invalid_index = ~0;
const auto invalid_file_index = (uint16_t)~0u;
const auto invalid_node_desc = node_desc_t{rt::node_type_e::None, utils::invalid_node_id};

struct data_state_t {

    // stable node ids to not update indices on node list mutation
    utils::index_id_list_t node_ids;

    utils::sparse_vector<file_node_t> nodes_file;
    utils::sparse_vector<node_random_t> nodes_random;
    utils::sparse_vector<node_sequence_t> nodes_sequence;
    utils::sparse_vector<node_repeat_t> nodes_repeat;

    std::vector<named_group_t> groups;
    std::vector<event_t> events;

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

/**
 * Data state API
 */

void init(data_state_t* state);

void create_group(data_state_t* state, size_t group_index);
void remove_group(data_state_t* state, size_t group_index);
inline const named_group_t& get_group(const data_state_t* state, size_t group_index) {
    return state->groups[group_index];
}
inline named_group_t& get_group_mut(data_state_t* state, size_t group_index) {
    return state->groups[group_index];
}

void create_node(data_state_t* state, const node_desc_t& desc);
void destroy_node(data_state_t* state, const node_desc_t& desc);

const file_node_t& get_file_node(const data_state_t* state, utils::index_id_t id);
file_node_t& get_file_node_mut(data_state_t* state, utils::index_id_t id);

const node_repeat_t& get_repeat_node(const data_state_t* state, utils::index_id_t id);
node_repeat_t& get_repeat_node_mut(data_state_t* state, utils::index_id_t id);

//
// data indexed getters
//

static void get_ptr_by_index(data_state_t& state, size_t index, event_t** out_ptr) {
    *out_ptr = &state.events[index];
}

static void get_ptr_by_index(data_state_t& state, size_t index, output_bus_t** out_ptr) {
    *out_ptr = &state.output_buses[index];
}

static void get_ptr_by_index(data_state_t& state, utils::index_id_t index, node_repeat_t** out_ptr) {
    *out_ptr = &get_repeat_node_mut(&state, index);
}

static void get_ptr_by_index(data_state_t& state, utils::index_id_t index, file_node_t** out_ptr) {
    *out_ptr = &get_file_node_mut(&state, index);
}

/////////////////////////////////////////////////////////////////////////////////////////

static const std::vector<node_desc_t>* get_child_nodes_ptr(const data_state_t* state, const node_desc_t& node) {
    auto node_index = get_index(state->node_ids, node.id);

    switch (node.type)
    {       
    case rt::node_type_e::Random:
        return &state->nodes_random[node_index].nodes;
    case rt::node_type_e::Sequence:
        return &state->nodes_sequence[node_index].nodes;
    default:
        // no child nodes
        break;
    }
    return nullptr;
}

static std::vector<node_desc_t>* get_child_nodes_ptr_mut(data_state_t* state, const node_desc_t& node) {
    return const_cast<std::vector<node_desc_t>*>(get_child_nodes_ptr(state, node));
}

struct audio_file_data_t {
    rt::file_data_t::meta_t meta;
    std::vector<uint8_t> content;
};

class audio_file_data_provider_ti {
public:
    virtual audio_file_data_t get_file_data(const char* filename, uint32_t file_index) = 0;
};

std::vector<uint8_t> save_store_blob_buffer(const data_state_t* state, audio_file_data_provider_ti* fdata_provider);

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

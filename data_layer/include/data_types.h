#pragma once

#include "sound_data_types_generated.h"
#include "sparse_vector.h"
#include "index_id.h"

namespace hle_audio {
namespace editor {

struct node_desc_t {
    NodeType type;
    utils::index_id_t id;
};

struct file_node_t {
    std::string filename;
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

struct output_bus_t {
    std::string name;
};

const size_t invalid_index = ~0;
const auto invalid_file_index = (uint16_t)~0u;
const auto invalid_node_desc = node_desc_t{NodeType_None, utils::invalid_node_id};

struct data_state_t {

    // stable node ids to not update indices on node list mutation
    utils::index_id_list_t node_ids;

    utils::sparse_vector<file_node_t> nodes_file;
    utils::sparse_vector<node_random_t> nodes_random;
    utils::sparse_vector<node_sequence_t> nodes_sequence;
    utils::sparse_vector<node_repeat_t> nodes_repeat;

    std::vector<named_group_t> groups;
    std::vector<EventT> events;

    std::vector<output_bus_t> output_buses;
};

static bool is_action_target_all(const ActionT& action) {
    return action.type == ActionType_stop_all;
}

static bool is_event_target_group(const EventT& event, size_t group_index) {
    for (auto& action_ptr : event.actions) {
        if (is_action_target_all(*action_ptr)) continue;

        if (group_index == action_ptr->target_group_index) {
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
inline named_group_t& get_group(data_state_t* state, size_t group_index) {
    return state->groups[group_index];
}

void create_node(data_state_t* state, const node_desc_t& desc);
void destroy_node(data_state_t* state, const node_desc_t& desc);

const file_node_t& get_file_node(const data_state_t* state, utils::index_id_t id);
file_node_t& get_file_node_mut(data_state_t* state, utils::index_id_t id);

const node_repeat_t& get_repeat_node(const data_state_t* state, utils::index_id_t id);
node_repeat_t& get_repeat_node_mut(data_state_t* state, utils::index_id_t id);

static const std::vector<node_desc_t>* get_child_nodes_ptr(const data_state_t* state, const node_desc_t& node) {
    auto node_index = get_index(state->node_ids, node.id);

    switch (node.type)
    {       
    case NodeType_Random:
        return &state->nodes_random[node_index].nodes;
    case NodeType_Sequence:
        return &state->nodes_sequence[node_index].nodes;
    }
    return nullptr;
}

static std::vector<node_desc_t>* get_child_nodes_ptr_mut(data_state_t* state, const node_desc_t& node) {
    return const_cast<std::vector<node_desc_t>*>(get_child_nodes_ptr(state, node));
}

std::vector<uint8_t> save_store_fb_buffer(const data_state_t* state);

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

#include "data_types.h"

namespace hle_audio {
namespace editor {

using utils::index_id_list_t;
using utils::index_id_t;

void init(data_state_t* state) {
    init(state->node_ids);

    output_bus_t bus = {};
    bus.name = "Default";
    state->output_buses.push_back(bus);
}

void create_group(data_state_t* state, size_t group_index) {
    auto& groups = state->groups;
    auto insert_it = groups.begin() + group_index;
    groups.insert(insert_it, named_group_t{});
    auto& name_ref = groups[group_index].name;
    name_ref = "Group " + std::to_string(group_index);

    // update indices
    if (group_index < groups.size() - 1) {
        auto& events = state->events;
        for (auto& event_ptr : events) {
            for (auto& action : event_ptr.actions) {
                if (is_action_target_all(action)) continue;

                if (group_index <= action.target_group_index) {
                    ++action.target_group_index;
                }
            }
        }
    }
}

void remove_group(data_state_t* state, size_t group_index) {
    auto& groups = state->groups;

    auto group_it = groups.begin() + group_index;
    groups.erase(group_it);

    // update indices
    auto& events = state->events;
    for (auto& event_ptr : events) {
        for (auto& action : event_ptr.actions) {
            if (is_action_target_all(action)) continue;

            assert(group_index != action.target_group_index);
            if (group_index < action.target_group_index) {
                --action.target_group_index;
            }
        }
    }
}

void create_node(data_state_t* state, const node_desc_t& desc) {
    size_t node_index = 0;
    switch (desc.type)
    {
    case NodeType_None: {
        break;
    }
    case NodeType_File: {
        node_index = state->nodes_file.add({});
        break;
    }
    case NodeType_Random: {
        node_index = state->nodes_random.add({});
        break;
    }
    case NodeType_Sequence: {
        node_index = state->nodes_sequence.add({});
        break;
    }
    case NodeType_Repeat: {
        node_index = state->nodes_repeat.add({});
        break;
    }
    default:
        assert(false);
        break;
    }

    store_index(state->node_ids, desc.id, node_index);
}

void destroy_node(data_state_t* state, const node_desc_t& desc) {
    auto node_index = (size_t)state->node_ids[desc.id];
    free_id(state->node_ids, desc.id);

    switch (desc.type)
    {
    case NodeType_None: {
        break;
    }
    case NodeType_File: {
        state->nodes_file.remove(node_index);
        break;
    }
    case NodeType_Random: {
        state->nodes_random.remove(node_index);
        break;
    }
    case NodeType_Sequence: {
        state->nodes_sequence.remove(node_index);
        break;
    }
    case NodeType_Repeat: {
        state->nodes_repeat.remove(node_index);
        break;
    }
    default:
        assert(false);
        break;
    }
}

const file_node_t& get_file_node(const data_state_t* state, index_id_t id) {
    auto node_index = (size_t)state->node_ids[id];
    return state->nodes_file[node_index];
}

file_node_t& get_file_node_mut(data_state_t* state, index_id_t id) {
    return const_cast<file_node_t&>(get_file_node(state, id));
}

const node_repeat_t& get_repeat_node(const data_state_t* state, utils::index_id_t id) {
    auto node_index = (size_t)state->node_ids[id];
    return state->nodes_repeat[node_index];
}

node_repeat_t& get_repeat_node_mut(data_state_t* state, utils::index_id_t id) {
    return const_cast<node_repeat_t&>(get_repeat_node(state, id));
}

}
}

#include "data_state.h"
#include <cassert>

namespace hle_audio {
namespace data {

using utils::index_id_list_t;
using utils::index_id_t;

void init(data_state_t* state) {
    assert(state->fnodes.is_empty());
    auto reserved_invalid_node = state->fnodes.add({});
    assert(reserved_invalid_node == invalid_node_id);

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
                if (!is_action_target_group(action.type)) continue;

                if (group_index <= action.target_index) {
                    ++action.target_index;
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
            if (!is_action_target_group(action.type)) continue;

            assert(group_index != action.target_index);
            if (group_index < action.target_index) {
                --action.target_index;
            }
        }
    }
}

node_id_t create_node(data_state_t* state, size_t group_index, flow_node_type_t type, vec2_t position) {
    size_t node_index = 0;
    switch (type)
    {
    case FILE_FNODE_TYPE: {
        node_index = state->fnodes_file.add({});
        break;
    }
    case RANDOM_FNODE_TYPE: {
        node_index = state->fnodes_random.add({});
        break;
    }
    default:
        assert(false);
        break;
    }

    common_flow_node_t fnode = {};
    fnode.type = type;
    fnode.position = position;
    fnode.index = node_index;
    auto node_id = node_id_t(state->fnodes.add(fnode));

    auto& group = state->groups[group_index];
    group.nodes.push_back(node_id);

    if (group.start_node == invalid_node_id) {
        group.start_node = node_id;
    }

    return node_id;
}

void destroy_node(data_state_t* state, size_t group_index, node_id_t node_id) {

    auto& group = state->groups[group_index];
    for (size_t i = 0; i < group.nodes.size(); ++i) {
        if (group.nodes[i] == node_id) {
            group.nodes[i] = group.nodes.back();
            group.nodes.pop_back();
            break;
        }
    }
    if (group.start_node == node_id) {
        group.start_node = group.nodes.size() ? group.nodes[0] : invalid_node_id;
    }

    auto fnode = state->fnodes[node_id];
    state->fnodes.remove(node_id);

    switch (fnode.type)
    {
    case FILE_FNODE_TYPE: {
        state->fnodes_file.remove(fnode.index);
        break;
    }
    case RANDOM_FNODE_TYPE: {
        state->fnodes_random.remove(fnode.index);
        break;
    }
    default:
        assert(false);
        break;
    }
}

const file_flow_node_t& get_file_node(const data_state_t* state, node_id_t id) {
    auto node_index = get_node_data(state, id).index;
    return state->fnodes_file[node_index];
}

file_flow_node_t& get_file_node_mut(data_state_t* state, node_id_t id) {
    return const_cast<file_flow_node_t&>(get_file_node(state, id));
}

const random_flow_node_t& get_random_node(const data_state_t* state, node_id_t id) {
    auto node_index = get_node_data(state, id).index;
    return state->fnodes_random[node_index];
}

random_flow_node_t& get_random_node_mut(data_state_t* state, node_id_t id) {
    return const_cast<random_flow_node_t&>(get_random_node(state, id));
}

}
}

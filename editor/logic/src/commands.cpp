#include "commands.h"
#include <cassert>
#include "data_state.h"

using hle_audio::data::data_state_t;
using hle_audio::data::event_t;

namespace hle_audio {
namespace editor {

std::unique_ptr<cmd_i> group_create_cmd_t::apply(data_state_t* state) const {
    create_group(state, group_index);
    return std::make_unique<group_remove_cmd_t>(group_index);
}

std::unique_ptr<cmd_i> group_remove_cmd_t::apply(data_state_t* state) const {
    remove_group(state, group_index);
    return std::make_unique<group_create_cmd_t>(group_index);
}

std::unique_ptr<cmd_i> group_update_cmd_t::apply(data_state_t* state) const {
    auto& obj_ptr = get_group_mut(state, group_index);

    auto reverse_cmd = std::make_unique<group_update_cmd_t>(group_index, obj_ptr);
    
    obj_ptr = data;

    return reverse_cmd;
}

std::unique_ptr<cmd_i> node_create_cmd_t::apply(data_state_t* state) const {
    auto node_id = create_node(state, group_index, type, position);
    return std::make_unique<node_destroy_cmd_t>(group_index, node_id);
}

std::unique_ptr<cmd_i> node_destroy_cmd_t::apply(data_state_t* state) const {
    auto node_data = get_node_data(state, node);
    destroy_node(state, group_index, node);

    return std::make_unique<node_create_cmd_t>(group_index, node_data.type, node_data.position);
}

std::unique_ptr<cmd_i> node_move_cmd_t::apply(data_state_t* state) const {
    auto node_data = get_node_data(state, node);

    auto reverse_cmd = std::make_unique<node_move_cmd_t>(node, node_data.position);

    state->fnodes[node].position = this->position;

    return reverse_cmd;
}

std::unique_ptr<cmd_i> event_create_cmd_t::apply(data_state_t* state) const {
    auto& elems = state->events;
    auto insert_it = elems.begin() + index;
    elems.insert(insert_it, event_t{});

    auto& name_ref = elems[index].name;
    name_ref = "Event " + std::to_string(index);

    return std::make_unique<event_remove_cmd_t>(index);
}

std::unique_ptr<cmd_i> event_remove_cmd_t::apply(data_state_t* state) const {
    auto& elems = state->events;

    auto it = elems.begin() + index;
    elems.erase(it);

    return std::make_unique<event_create_cmd_t>(index);
}

}
}

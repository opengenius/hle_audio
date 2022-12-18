#include "commands.h"
#include <cassert>

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

std::unique_ptr<cmd_i> group_update_node_cmd_t::apply(data_state_t* state) const {
    auto& obj_ptr = get_group_mut(state, group_index);

    // make reverse command
    auto rev_cmd = std::make_unique<group_update_node_cmd_t>(group_index, obj_ptr.node);

    // update node
    obj_ptr.node = node_desc;

    return rev_cmd;
}

std::unique_ptr<cmd_i> node_create_cmd_t::apply(data_state_t* state) const {
    create_node(state, node_desc);
    return std::make_unique<node_destroy_cmd_t>(node_desc);
}

std::unique_ptr<cmd_i> node_destroy_cmd_t::apply(data_state_t* state) const {
    destroy_node(state, node_desc);

    return std::make_unique<node_create_cmd_t>(node_desc);
}

std::unique_ptr<cmd_i> node_add_child_cmd_t::apply(data_state_t* state) const {
    auto nodes_ptr = get_child_nodes_ptr_mut(state, node);
    assert(nodes_ptr);

    nodes_ptr->insert(nodes_ptr->begin() + child_index, child_node);

    auto rev_cmd = std::make_unique<node_remove_child_cmd_t>();
    rev_cmd->node = node;
    rev_cmd->child_index = child_index;
    return rev_cmd;
}

std::unique_ptr<cmd_i> node_remove_child_cmd_t::apply(data_state_t* state) const {
    auto nodes_ptr = get_child_nodes_ptr_mut(state, node);
    assert(nodes_ptr);
    
    auto child_desc = nodes_ptr->at(child_index);
    nodes_ptr->erase(nodes_ptr->begin() + child_index);

    auto rev_cmd = std::make_unique<node_add_child_cmd_t>();
    rev_cmd->node = node;
    rev_cmd->child_node = child_desc;
    rev_cmd->child_index = child_index;
    return rev_cmd;
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

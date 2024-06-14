#include "app_logic.h"
#include "data_state.h"
#include "commands.h"
#include <cassert>

using hle_audio::data::named_group_t;
using hle_audio::data::event_t;
using hle_audio::data::node_id_t;
using hle_audio::data::output_bus_t;
using hle_audio::data::file_flow_node_t;
using hle_audio::data::random_flow_node_t;

namespace hle_audio {
namespace editor {

static void execute_cmd_first(logic_state_t* state, std::unique_ptr<cmd_i> cmd) {
    push_chain_start(&state->cmds);
    execute_cmd(&state->cmds, &state->data_state, std::move(cmd));
}

static void execute_cmd(logic_state_t* state, std::unique_ptr<cmd_i> cmd) {
    execute_cmd(&state->cmds, &state->data_state, std::move(cmd));
}

void apply_group_update(logic_state_t* state, 
        size_t group_index, const named_group_t& data) {
    auto cmd = std::make_unique<group_update_cmd_t>(group_index, data);
    execute_cmd_first(state, std::move(cmd));
}

void add_event_action(logic_state_t* state, size_t event_index, size_t target_group_index) {
    auto event = state->data_state.events[event_index];

    rt::action_t act = {};
    if (target_group_index != data::invalid_index) {
        act.target_index = target_group_index;
    }
    
    event.actions.push_back(act);

    update_event(state, event_index, event);
}

void remove_event_action(logic_state_t* state, size_t event_index, size_t action_index) {
    auto event = state->data_state.events[event_index];
    event.actions.erase(event.actions.begin() + action_index);
    
    update_event(state, event_index, event);
}

void update_event(logic_state_t* state, size_t event_index, 
        const event_t& event_state) {
    auto cmd = std::make_unique<event_update_cmd_t>(event_index, event_state);
    execute_cmd_first(state, std::move(cmd));
}

void init(logic_state_t* state) {
    init(&state->data_state);    
}

void create_group(logic_state_t* state, size_t group_index) {
    auto cmd = std::make_unique<group_create_cmd_t>(group_index);
    execute_cmd_first(state, std::move(cmd));
}

static void perform_remove_node(logic_state_t* state, size_t group_index, data::node_id_t node) {
    auto& data_state = state->data_state;

    // reset node state
    auto ndata = get_node_data(&data_state, node);
    if (ndata.type == data::FILE_FNODE_TYPE) {
        execute_cmd(state, 
                std::make_unique<node_file_update_cmd_t>(node, data::file_flow_node_t{}));

    } else if (ndata.type == data::RANDOM_FNODE_TYPE) {
        execute_cmd(state, 
                std::make_unique<node_random_update_cmd_t>(node, data::random_flow_node_t{}));

    }

    // destroy node
    execute_cmd(state, 
            std::make_unique<node_destroy_cmd_t>(group_index, node));
}

static void perform_remove_event(logic_state_t* state, size_t index) {
    // reset params command to restore on undo
    execute_cmd(state, 
            std::make_unique<event_update_cmd_t>(index, event_t{}));

    execute_cmd(state, 
        std::make_unique<event_remove_cmd_t>(index));
}

/**
 * kind of root use case here, requires every command
 */
void remove_group(logic_state_t* state, size_t group_index) {
    auto& data_state = state->data_state;

    auto nodes = data_state.groups[group_index].nodes;
    
    // reset group params command to restore on undo
    execute_cmd_first(state, 
            std::make_unique<group_update_cmd_t>(group_index, named_group_t{}));

    // remove nodes commands
    for (auto& node_id : nodes) {
        perform_remove_node(state, group_index, node_id);
    }
    
    // remove events
    auto& events = data_state.events;
    for (size_t event_index = 0; event_index < events.size(); ++event_index) {
        auto& ev_ptr = events[event_index];
        if (is_event_target_group(ev_ptr, group_index)) {
            perform_remove_event(state, event_index);
            --event_index;
        }
    }

    execute_cmd(state, 
        std::make_unique<group_remove_cmd_t>(group_index));
}

void create_node(logic_state_t* state, size_t group_index, data::flow_node_type_t type, const data::vec2_t& position) {
    execute_cmd_first(state, 
            std::make_unique<node_create_cmd_t>(group_index, type, position));
}

static void remove_node_links(named_group_t& group, data::node_id_t node) {
    for (int i = 0; i < group.links.size(); ++i) {
        auto l = group.links[i];
        if (l.from == node || l.to == node) {
            group.links[i] = group.links.back();
            group.links.pop_back();
            --i;
        }
    }
}

void remove_nodes(logic_state_t* state, size_t group_index, std::span<const data::node_id_t> node_ids) {
    auto& data_state = state->data_state;

    push_chain_start(&state->cmds);

    // remove links
    auto group = data_state.groups[group_index];
    for (auto node : node_ids) {
        remove_node_links(group, node);
    }
    execute_cmd(state, 
                std::make_unique<group_update_cmd_t>(group_index, group));

    for (auto node : node_ids) {
        perform_remove_node(state, group_index, node);
    }
}

void assign_file_node_file(logic_state_t* state, node_id_t node_id, const std::u8string& filename) {
    auto node = get_file_node(&state->data_state, node_id);

    node.filename = filename;

    auto cmd = std::make_unique<node_file_update_cmd_t>(node_id, node);
    execute_cmd_first(state, std::move(cmd));
}

void update_file_node(logic_state_t* state, node_id_t node_id, const file_flow_node_t& data) {
    auto cmd = std::make_unique<node_file_update_cmd_t>(node_id, data);
    execute_cmd_first(state, std::move(cmd));
}

void update_random_node(logic_state_t* state, node_id_t node_id, const random_flow_node_t& data) {
    auto cmd = std::make_unique<node_random_update_cmd_t>(node_id, data);
    execute_cmd_first(state, std::move(cmd));
}

void move_nodes(logic_state_t* state, std::span<const data::node_id_t> node_ids, std::span<const data::vec2_t> positions) {
    assert(node_ids.size() == positions.size());

    push_chain_start(&state->cmds);

    for (auto& node_id : node_ids) {
        auto it_index = &node_id - node_ids.data();

        execute_cmd(state, 
                std::make_unique<node_move_cmd_t>(node_id, positions[it_index]));
    }
    
}

void create_event(logic_state_t* state, size_t index) {
    auto cmd = std::make_unique<event_create_cmd_t>(index);
    execute_cmd_first(state, std::move(cmd));
}

void remove_event(logic_state_t* state, size_t index) {
    auto& data_state = state->data_state;
    
    push_chain_start(&state->cmds);
    perform_remove_event(state, index);
}

void rename_bus(logic_state_t* state, size_t bus_index, const char* new_name) {
    output_bus_t bus = {};
    bus.name = new_name;
    execute_cmd_first(state, 
        std::make_unique<bus_update_cmd_t>(bus_index, bus));
}

}
}

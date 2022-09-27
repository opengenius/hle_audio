#include "app_logic.h"
#include "commands.h"

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

    hle_audio::ActionT act = {};
    if (target_group_index != invalid_index) {
        act.target_group_index = target_group_index;
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

static void perform_remove_node_rec(logic_state_t* state, const node_desc_t& node_desc) {
    // detach children and remove recursively
    auto nodes_ptr = get_child_nodes_ptr(&state->data_state, node_desc);
    if (nodes_ptr) {
        while (nodes_ptr->size()) {
            auto child_desc = nodes_ptr->back();

            auto cmd = std::make_unique<node_remove_child_cmd_t>();
            cmd->node = node_desc;
            cmd->child_index = nodes_ptr->size() - 1;
            execute_cmd(state, std::move(cmd));

            perform_remove_node_rec(state, child_desc);
        }
    }

    // reset params
    if (node_desc.type == NodeType_File) {
        execute_cmd(state, 
            std::make_unique<node_file_update_cmd_t>(node_desc.id, file_node_t{}));
    } else if (node_desc.type == NodeType_Repeat) {
        auto node = get_repeat_node(&state->data_state, node_desc.id);

        auto cmd = std::make_unique<node_repeat_update_cmd_t>(
            node_desc.id, node_repeat_t{}
        );
        execute_cmd(state, std::move(cmd));

        perform_remove_node_rec(state, node.node);
    }
    // no params to reset yet
    // else if (node_desc.type() == NodeType_Random) {   
    // } else if (node_desc.type() == NodeType_Sequence) {
    // }

    execute_cmd(state, std::make_unique<node_destroy_cmd_t>(node_desc));
}

static void perform_remove_group_root_node_chained(logic_state_t* state, size_t group_index) {
    auto& data_state = state->data_state;

    auto& group_ptr = get_group(&data_state, group_index);
    if (group_ptr.node.type != NodeType_None) {
        auto node_desc = group_ptr.node;
        execute_cmd(state, 
            std::make_unique<group_update_node_cmd_t>(group_index, invalid_node_desc));
        perform_remove_node_rec(state, node_desc);
    }  
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
    
    // reset params command to restore on undo
    // first command, every next one to be chained
    execute_cmd_first(state, 
            std::make_unique<group_update_cmd_t>(group_index, named_group_t{}));

    // remove nodes commands
    perform_remove_group_root_node_chained(state, group_index);

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

static node_desc_t execute_cmd_create_node(logic_state_t* state, NodeType type) {
    node_desc_t desc = {
        type,
        reserve_node_id(state->data_state.node_ids)
    };
        
    auto cmd = std::make_unique<node_create_cmd_t>(desc);
    execute_cmd_first(state, std::move(cmd));

    return desc;
}

void create_root_node(logic_state_t* state, size_t group_index, NodeType type) {
    auto desc = execute_cmd_create_node(state, type);
    auto cmd = std::make_unique<group_update_node_cmd_t>(group_index, desc);
    execute_cmd(state, std::move(cmd));
}

void create_node(logic_state_t* state, const node_desc_t& node_desc, NodeType type) {
    auto nodes_ptr = get_child_nodes_ptr(&state->data_state, node_desc);
    assert(nodes_ptr);

    auto child_desc = execute_cmd_create_node(state, type);

    auto cmd = std::make_unique<node_add_child_cmd_t>();
    cmd->node = node_desc;
    cmd->child_node = child_desc;
    cmd->child_index = nodes_ptr->size();
    execute_cmd(state, std::move(cmd));
}

void create_repeat_node(logic_state_t* state, const node_desc_t& node_desc, NodeType type) {
    auto node = get_repeat_node(&state->data_state, node_desc.id);
    node.node = execute_cmd_create_node(state, type);

    auto cmd = std::make_unique<node_repeat_update_cmd_t>(
        node_desc.id, node
    );
    execute_cmd(state, std::move(cmd));
}

void remove_root_node(logic_state_t* state, size_t group_index) {
    // start commands
    push_chain_start(&state->cmds);

    // remove nodes commands
    perform_remove_group_root_node_chained(state, group_index);
}

void remove_node(logic_state_t* state, const node_desc_t& parent_node_desc, size_t node_index) {
    if (parent_node_desc.type == NodeType_Repeat) {
        auto node = get_repeat_node(&state->data_state, parent_node_desc.id);

        auto child_desc = node.node;
        node.node = {};

        auto cmd = std::make_unique<node_repeat_update_cmd_t>(parent_node_desc.id, node);
        execute_cmd_first(state, std::move(cmd));
        perform_remove_node_rec(state, child_desc);
    } else {
        // detach child and remove recursively
        auto nodes_ptr = get_child_nodes_ptr(&state->data_state, parent_node_desc);
        if (nodes_ptr) {
            if  (node_index < nodes_ptr->size()) {
                auto child_desc = nodes_ptr->at(node_index);

                auto cmd = std::make_unique<node_remove_child_cmd_t>();
                cmd->node = parent_node_desc;
                cmd->child_index = node_index;
                execute_cmd_first(state, std::move(cmd));

                perform_remove_node_rec(state, child_desc);
            }
        }
    }
}

void assign_file_node_file(logic_state_t* state, const node_desc_t& node_desc, const std::string& filename) {
    auto node = get_file_node(&state->data_state, node_desc.id);

    node.filename = filename;

    auto cmd = std::make_unique<node_file_update_cmd_t>(node_desc.id, node);
    execute_cmd_first(state, std::move(cmd));
}

void switch_file_node_loop(logic_state_t* state, const node_desc_t& node_desc) {
    auto node = get_file_node(&state->data_state, node_desc.id);
    
    node.loop = !node.loop;

    auto cmd = std::make_unique<node_file_update_cmd_t>(node_desc.id, node);
    execute_cmd_first(state, std::move(cmd));
}

void switch_file_node_stream(logic_state_t* state, const node_desc_t& node_desc) {
    auto node = get_file_node(&state->data_state, node_desc.id);

    node.stream = !node.stream;

    auto cmd = std::make_unique<node_file_update_cmd_t>(node_desc.id, node);
    execute_cmd_first(state, std::move(cmd));
}

void update_repeat_node_times(logic_state_t* state, const node_desc_t& node_desc, uint16_t times) {
    auto node = get_repeat_node(&state->data_state, node_desc.id);

    node.repeat_count = times;

    auto cmd = std::make_unique<node_repeat_update_cmd_t>(node_desc.id, node);
    execute_cmd_first(state, std::move(cmd));
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

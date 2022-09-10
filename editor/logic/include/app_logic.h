#pragma once

#include "data_types.h"
#include "cmd_stack.h"

namespace hle_audio {
namespace editor {

struct logic_state_t {
    
    // command stack
    command_stack_t cmds;

    // data layer    
    data_state_t data_state;
};

void init(logic_state_t* state);

void create_group(logic_state_t* state, size_t group_index);
void remove_group(logic_state_t* state, size_t group_index);
void apply_group_update(logic_state_t* state, size_t group_index, const named_group_t& data);

void create_root_node(logic_state_t* state, size_t group_index, NodeType type);
void remove_root_node(logic_state_t* state, size_t group_index);
void create_node(logic_state_t* state, const node_desc_t& node_desc, NodeType type);
void remove_node(logic_state_t* state, const node_desc_t& parent_node_desc, size_t node_index);

void create_repeat_node(logic_state_t* state, const node_desc_t& node_desc, NodeType type);

void assign_file_node_file(logic_state_t* state, const node_desc_t& node_desc, const std::string& filename);
void switch_file_node_loop(logic_state_t* state, const node_desc_t& node_desc);
void switch_file_node_stream(logic_state_t* state, const node_desc_t& node_desc);

void update_repeat_node_times(logic_state_t* state, const node_desc_t& node_desc, uint16_t times);

void create_event(logic_state_t* state, size_t index);
void remove_event(logic_state_t* state, size_t index);
void update_event(logic_state_t* state, size_t event_index, 
        const char* name);

size_t add_event_action(logic_state_t* state, size_t event_index, size_t target_group_index);
void remove_event_action(logic_state_t* state, size_t event_index, size_t action_index);
void update_event_action(logic_state_t* state,
        size_t event_index, size_t action_index,
        const ActionT& action);

void rename_bus(logic_state_t* state, size_t bus_index, const char* new_name);

}
}

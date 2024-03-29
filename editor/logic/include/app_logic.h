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

void create_root_node(logic_state_t* state, size_t group_index, rt::node_type_e type);
void remove_root_node(logic_state_t* state, size_t group_index);
void create_node(logic_state_t* state, const node_desc_t& node_desc, rt::node_type_e type);
void remove_node(logic_state_t* state, const node_desc_t& parent_node_desc, size_t node_index);

void create_repeat_node(logic_state_t* state, const node_desc_t& node_desc, rt::node_type_e type);

void assign_file_node_file(logic_state_t* state, const node_desc_t& node_desc, const std::u8string& filename);
void update_file_node(logic_state_t* state, const node_desc_t& node_desc, const file_node_t& data);

void update_repeat_node(logic_state_t* state, const node_desc_t& node_desc, const node_repeat_t& data);

void create_event(logic_state_t* state, size_t index);
void remove_event(logic_state_t* state, size_t index);
void update_event(logic_state_t* state, size_t event_index, 
        const event_t& event_state);

void add_event_action(logic_state_t* state, size_t event_index, size_t target_group_index);
void remove_event_action(logic_state_t* state, size_t event_index, size_t action_index);

void rename_bus(logic_state_t* state, size_t bus_index, const char* new_name);

}
}

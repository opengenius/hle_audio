#pragma once

#include "data_types.h"
#include "cmd_stack.h"
#include <span>

namespace hle_audio {
namespace editor {

struct logic_state_t {
    
    // command stack
    command_stack_t cmds;

    // data layer    
    data::data_state_t data_state;
};

void init(logic_state_t* state);

void create_group(logic_state_t* state, size_t group_index);
void remove_group(logic_state_t* state, size_t group_index);
void apply_group_update(logic_state_t* state, size_t group_index, const data::named_group_t& data);

void create_node(logic_state_t* state, size_t group_index, data::flow_node_type_t type, const data::vec2_t& position);
void remove_nodes(logic_state_t* state, size_t group_index, std::span<const data::node_id_t> node_ids);

void assign_file_node_file(logic_state_t* state, data::node_id_t node_id, const std::u8string& filename);
void update_file_node(logic_state_t* state, data::node_id_t node_id, const data::file_flow_node_t& data);

void update_random_node(logic_state_t* state, data::node_id_t node_id, const data::random_flow_node_t& data);

void move_nodes(logic_state_t* state, std::span<const data::node_id_t> node_ids, std::span<const data::vec2_t> positions);

void create_event(logic_state_t* state, size_t index);
void remove_event(logic_state_t* state, size_t index);
void update_event(logic_state_t* state, size_t event_index, 
        const data::event_t& event_state);

void add_event_action(logic_state_t* state, size_t event_index, size_t target_group_index);
void remove_event_action(logic_state_t* state, size_t event_index, size_t action_index);

void rename_bus(logic_state_t* state, size_t bus_index, const char* new_name);

}
}

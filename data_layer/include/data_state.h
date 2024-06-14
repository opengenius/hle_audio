#pragma once

#include "data_types.h"

namespace hle_audio {
namespace data {

/**
 * Data state API
 */

void init(data_state_t* state);

void create_group(data_state_t* state, size_t group_index);
void remove_group(data_state_t* state, size_t group_index);
inline const named_group_t& get_group(const data_state_t* state, size_t group_index) {
    return state->groups[group_index];
}
inline named_group_t& get_group_mut(data_state_t* state, size_t group_index) {
    return state->groups[group_index];
}

node_id_t create_node(data_state_t* state, size_t group_index, flow_node_type_t type, vec2_t position);
void destroy_node(data_state_t* state, size_t group_index, node_id_t node_id);

const file_flow_node_t& get_file_node(const data_state_t* state, node_id_t id);
file_flow_node_t& get_file_node_mut(data_state_t* state, node_id_t id);

const random_flow_node_t& get_random_node(const data_state_t* state, node_id_t id);
random_flow_node_t& get_random_node_mut(data_state_t* state, node_id_t id);

//
// data indexed getters
//

static common_flow_node_t get_node_data(const data_state_t* state, node_id_t node_id) {
    return state->fnodes[node_id];
}

static void get_ptr_by_index(data_state_t& state, size_t index, event_t** out_ptr) {
    *out_ptr = &state.events[index];
}

static void get_ptr_by_index(data_state_t& state, size_t index, output_bus_t** out_ptr) {
    *out_ptr = &state.output_buses[index];
}

static void get_ptr_by_index(data_state_t& state, node_id_t id, random_flow_node_t** out_ptr) {
    *out_ptr = &get_random_node_mut(&state, id);
}

static void get_ptr_by_index(data_state_t& state, node_id_t id, file_flow_node_t** out_ptr) {
    *out_ptr = &get_file_node_mut(&state, id);
}

}
}

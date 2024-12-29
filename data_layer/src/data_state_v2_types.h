#pragma once

#include "data_types.h"

namespace hle_audio {
namespace data {

namespace v2 {

struct named_group_t {
    std::string name = {};
    float volume = 1.0f;
    float cross_fade_time = 0.0f;
    uint8_t output_bus_index = 0;
    node_id_t start_node;
    std::vector<node_id_t> nodes;
    std::vector<link_t> links;
};

struct data_state_t {
    std::vector<named_group_t> groups;
    std::vector<event_t> events;

    utils::sparse_vector<common_flow_node_t> fnodes;
    utils::sparse_vector<file_flow_node_t> fnodes_file;
    utils::sparse_vector<random_flow_node_t> fnodes_random;

    std::vector<output_bus_t> output_buses;
};

}

}}

#pragma once

#include "cmd_types.h"
#include "data_types.h"

namespace hle_audio {
namespace editor {

/**
 * create group
 */
class group_create_cmd_t : public cmd_i {
    size_t group_index;

public:
    group_create_cmd_t(size_t group_index) {
        this->group_index = group_index;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

/**
 * remove group
 */
class group_remove_cmd_t : public cmd_i {
    size_t group_index;

public:
    group_remove_cmd_t(size_t group_index) {
        this->group_index = group_index;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

/**
 * update group
 */
class group_update_cmd_t : public cmd_i {
    size_t group_index;

    data::named_group_t data;
    
public:
    group_update_cmd_t(size_t group_index, const data::named_group_t& data) {
        this->group_index = group_index;
        this->data = data;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;    
};

/**
 * Nodes
 */
class node_create_cmd_t : public cmd_i {
    size_t group_index;
    data::flow_node_type_t type;
    data::vec2_t position;

public:
    node_create_cmd_t(size_t group_index, data::flow_node_type_t type, data::vec2_t position) {
        this->group_index = group_index;
        this->type = type;
        this->position = position;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

class node_destroy_cmd_t : public cmd_i {
    size_t group_index;
    data::node_id_t node;

public:
    node_destroy_cmd_t(size_t group_index, data::node_id_t node) {
        this->group_index = group_index;
        this->node = node;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

class node_move_cmd_t : public cmd_i {
    data::node_id_t node;
    data::vec2_t position;

public:
    node_move_cmd_t(data::node_id_t node, data::vec2_t position) {
        this->node = node;
        this->position = position;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

/**
 * Events
 */
class event_create_cmd_t : public cmd_i {
    size_t index;

public:
    event_create_cmd_t(size_t index) {
        this->index = index;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

class event_remove_cmd_t : public cmd_i {
    size_t index;

public:
    event_remove_cmd_t(size_t index) {
        this->index = index;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override;
};

template<typename T, typename IndexT = size_t>
class update_by_index_cmd_t : public cmd_i {
    IndexT index;
    T data;

public:
    update_by_index_cmd_t(IndexT index, const T& data) {
        this->index = index;
        this->data = data;
    }

    std::unique_ptr<cmd_i> apply(data::data_state_t* state) const override {
        T* item_ptr;
        get_ptr_by_index(*state, index, &item_ptr);

        auto reverse_cmd = std::make_unique<update_by_index_cmd_t>(index, *item_ptr);
        *item_ptr = data;

        return reverse_cmd;
    }
};

using node_random_update_cmd_t = update_by_index_cmd_t<data::random_flow_node_t, data::node_id_t>;
using node_file_update_cmd_t = update_by_index_cmd_t<data::file_flow_node_t, data::node_id_t>;
using event_update_cmd_t = update_by_index_cmd_t<data::event_t>;
using bus_update_cmd_t = update_by_index_cmd_t<data::output_bus_t>;

}
}

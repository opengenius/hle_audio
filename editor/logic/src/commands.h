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

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
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

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

/**
 * update group
 */
class group_update_cmd_t : public cmd_i {
    size_t group_index;

    named_group_t data;
    
public:
    group_update_cmd_t(size_t group_index, const named_group_t& data) {
        this->group_index = group_index;
        this->data = data;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;    
};

class group_update_node_cmd_t : public cmd_i {
    size_t group_index;
    node_desc_t node_desc;

public:
    group_update_node_cmd_t(size_t group_index, const node_desc_t& node_desc) {
        this->group_index = group_index;
        this->node_desc = node_desc;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

/**
 * Nodes
 */
class node_create_cmd_t : public cmd_i {
    node_desc_t node_desc;

public:
    node_create_cmd_t(const node_desc_t& node_desc) {
        this->node_desc = node_desc;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class node_destroy_cmd_t : public cmd_i {
    node_desc_t node_desc;

public:
    node_destroy_cmd_t(const node_desc_t& node_desc) {
        this->node_desc = node_desc;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class node_add_child_cmd_t : public cmd_i {
public:
    node_desc_t node;
    node_desc_t child_node;
    size_t child_index;

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class node_remove_child_cmd_t : public cmd_i {
public:
    node_desc_t node;
    size_t child_index;

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
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

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class event_remove_cmd_t : public cmd_i {
    size_t index;

public:
    event_remove_cmd_t(size_t index) {
        this->index = index;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
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

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override {
        T* item_ptr;
        get_ptr_by_index(*state, index, &item_ptr);

        auto reverse_cmd = std::make_unique<update_by_index_cmd_t>(index, *item_ptr);
        *item_ptr = data;

        return reverse_cmd;
    }
};

using node_repeat_update_cmd_t = update_by_index_cmd_t<node_repeat_t, utils::index_id_t>;
using node_file_update_cmd_t = update_by_index_cmd_t<file_node_t, utils::index_id_t>;
using event_update_cmd_t = update_by_index_cmd_t<event_t>;
using bus_update_cmd_t = update_by_index_cmd_t<output_bus_t>;

}
}

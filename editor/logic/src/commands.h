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

class node_file_update_cmd_t : public cmd_i {
    utils::index_id_t index;
    file_node_t node_data;

public:
    node_file_update_cmd_t(utils::index_id_t index, 
            const file_node_t& node_data) {
        this->index = index;
        this->node_data = node_data;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class node_repeat_update_cmd_t : public cmd_i {
    utils::index_id_t index;
    node_repeat_t node_data;

public:
    node_repeat_update_cmd_t(utils::index_id_t index, 
            const node_repeat_t& node_data) {
        this->index = index;
        this->node_data = node_data;
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
class event_update_cmd_t : public cmd_i {
    size_t index;
    std::string name;
    
public:
    event_update_cmd_t(size_t index, const std::string& name) {
        this->index = index;
        this->name = name;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;    
};

class event_action_add_cmd_t : public cmd_i {
    size_t index;
    size_t action_index;
    
public:
    event_action_add_cmd_t(size_t index, size_t action_index) {
        this->index = index;
        this->action_index = action_index;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class event_action_remove_cmd_t : public cmd_i {
    size_t index;
    size_t action_index;
    
public:
    event_action_remove_cmd_t(size_t index, size_t action_index) {
        this->index = index;
        this->action_index = action_index;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class event_action_update_cmd_t : public cmd_i {
    size_t index;
    size_t action_index;
    ActionT action;
    
public:
    event_action_update_cmd_t(size_t index, size_t action_index, const ActionT& action) {
        this->index = index;
        this->action_index = action_index;
        this->action = action;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

class bus_update_cmd_t : public cmd_i {
    size_t index;
    output_bus_t data;
    
public:
    bus_update_cmd_t(size_t index, const output_bus_t& data) {
        this->index = index;
        this->data = data;
    }

    std::unique_ptr<cmd_i> apply(data_state_t* state) const override;
};

}
}

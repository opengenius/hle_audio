#pragma once

#include <vector>
#include "data_types.h"

namespace hle_audio {
namespace editor {

// should not be here, it's domain level actions
enum class view_action_type_e {
    NONE,
    SAVE,
    UNDO,
    REDO,

    GROUP_ADD,
    GROUP_REMOVE,
    APPLY_SELECTED_GROUP_UPDATE,

    NODE_UPDATE,
    NODE_ADD,

    EVENT_ADD,
    EVENT_REMOVE,
    EVENT_UPDATE,
    EVENT_ADD_ACTION,
    EVENT_REMOVE_ACTION,
    EVENT_FILTER,

    REFRESH_SOUND_LIST,

    BUS_ADD,
    BUS_RENAME,
    BUS_VOLUME_CHANGED,

    SOUND_PLAY,
    SOUND_STOP,

    RUNTIME_FIRE_EVENT,
    RUNTIME_FIRE_GROUP_STOP,
    RUNTIME_FIRE_GROUP_STOP_ALL,
    RUNTIME_FIRE_GROUP_STOP_BUS,
    Count
};

struct bus_edit_data_t {
    std::string name;
};

struct node_action_data_t {
    node_desc_t node_desc;

    view_action_type_e action;
    union {
        uint16_t repeat_count;
    } action_data;

    bool action_add;
    bool action_assign_sound;
    bool action_switch_loop;
    bool action_switch_stream;

    node_desc_t parent_node_desc;
    uint32_t node_index;
    bool action_remove;
};

struct view_state_t {
    float scale;

    float root_pane_width_scaled;
    bool select_events_tab = false;

    bool has_save = false;
    bool has_undo = false;
    bool has_redo = false;
    bool has_wav_playing = false;

    // groups
    size_t active_group_index = invalid_index;
    named_group_t selected_group_state;

    // event list
    std::string event_filter_str;
    size_t event_filter_group_index = invalid_index;
    size_t groups_size_on_event_filter_group;
    std::vector<size_t> filtered_event_indices;
    size_t event_list_index = invalid_index;

    // active event
    size_t active_event_index = invalid_index;
    event_t event_state;
    size_t event_action_cmd_index;

    // add node popup
    node_desc_t add_node_target;

    // sound file list
    const std::vector<std::string>* sound_files_u8_names_ptr;
    size_t selected_sound_file_index = invalid_index;

    std::vector<int> output_bus_volumes;
    bus_edit_data_t bus_edit_state;
    size_t action_bus_index;

    struct group_info_t {
        size_t group_index;
        bool paused;
    };
    std::vector<group_info_t> active_group_infos;
    size_t runtime_target_index;

    // actions
    NodeType add_node_type;    
    node_action_data_t node_action;
    size_t action_group_index;
};

view_action_type_e build_view(view_state_t& mut_view_state, const data_state_t& data_state);

}
}

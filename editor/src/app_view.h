#pragma once

#include <vector>
#include "data_types.h"
#include <variant>

namespace hle_audio {
namespace editor {

// should not be here, it's domain level actions
enum class view_action_type_e {
    NONE,
    SAVE,
    UNDO,
    REDO,
    SAVE_AND_EXIT,
    EXIT,

    GROUP_ADD,
    GROUP_REMOVE,
    APPLY_SELECTED_GROUP_UPDATE,

    NODE_UPDATE,
    NODE_ADD,
    NODE_REMOVE,
    NODE_FILE_ASSIGN_SOUND,

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
    RUNTIME_FIRE_GROUP_PAUSE,
    RUNTIME_FIRE_GROUP_RESUME,
    RUNTIME_FIRE_GROUP_PAUSE_BUS,
    RUNTIME_FIRE_GROUP_RESUME_BUS,
    Count
};

struct bus_edit_data_t {
    std::string name;
};

struct node_action_data_t {
    node_desc_t parent_node_desc;
    node_desc_t node_desc;

    std::variant<
        rt::node_type_e, // NODE_ADD
        uint32_t,        // NODE_REMOVE
        file_node_t,     // NODE_UPDATE
        node_repeat_t
        > action_data;
};

struct view_state_t {
    float scale;

    float root_pane_width_scaled;
    bool select_events_tab = false;
    bool focus_selected_event = false;
    bool focus_selected_group = false;
    bool apply_edit_focus_on_group = false;
    bool apply_edit_focus_on_event = false;
    bool show_exit_save_dialog = false;

    bool has_save = false;
    bool has_undo = false;
    bool has_redo = false;
    bool has_wav_playing = false;

    struct filtered_indices_list_state_t {
        std::vector<size_t> indices;
        size_t list_index = invalid_index;

        size_t get_index(size_t index) {
            // empty indices is considered as no filter
            return indices.size() ? indices[index] : index;
        }
    };

    // group list
    /* 
        better be implemented with optimal call to filter_<events|groups> in update_mutable_view_state
    std::string group_filter_str;
    filtered_indices_list_state_t group_filtered_state;
    */

    // groups
    size_t active_group_index = invalid_index;
    named_group_t selected_group_state;

    // event list
    std::string event_filter_str;
    size_t event_filter_group_index = invalid_index;
    size_t groups_size_on_event_filter_group;
    filtered_indices_list_state_t events_filtered_state;

    // active event
    size_t active_event_index = invalid_index;
    event_t event_state;
    size_t event_action_cmd_index;

    // sound file list
    const std::vector<std::string>* sound_files_u8_names_ptr;
    size_t selected_sound_file_index = invalid_index;

    std::vector<int> output_bus_volumes;
    bus_edit_data_t bus_edit_state = {};
    size_t action_bus_index;

    struct group_info_t {
        size_t group_index;
        bool paused;
    };
    std::vector<group_info_t> active_group_infos;
    size_t runtime_target_index;

    // actions
    node_action_data_t node_action = {};
    size_t action_group_index;
};

view_action_type_e build_view(view_state_t& mut_view_state, const data_state_t& data_state);

}
}

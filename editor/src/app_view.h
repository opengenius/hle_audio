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
    GROUP_FILTER,

    NODE_UPDATE,
    NODE_ADD,
    NODE_REMOVE,
    NODE_FILE_ASSIGN_SOUND,
    NODE_MOVED,

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
    data::node_id_t node_id;

    data::vec2_t add_position;

    std::variant<
        data::flow_node_type_t,  // NODE_ADD
        uint32_t,         // NODE_REMOVE
        data::file_flow_node_t, // NODE_UPDATE
        data::random_flow_node_t
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

    bool option_select_group_for_event = true;

    bool make_node_file_assign_action = false;

    struct filtered_indices_list_state_t {
        std::vector<size_t> indices;
        size_t list_index = data::invalid_index;

        size_t get_index(size_t index) {
            // empty indices is considered as no filter
            return indices.size() ? indices[index] : index;
        }
    };

    // group list
    std::string group_filter_str;
    filtered_indices_list_state_t group_filtered_state;

    // groups
    size_t active_group_index = data::invalid_index;
    data::named_group_t selected_group_state;
    size_t selected_group_state_revison;

    // event list
    std::string event_filter_str;
    size_t event_filter_group_index = data::invalid_index;
    size_t groups_size_on_event_filter_group;
    filtered_indices_list_state_t events_filtered_state;

    // active event
    size_t active_event_index = data::invalid_index;
    data::event_t event_state;
    size_t event_action_cmd_index;

    // sound file list
    const std::vector<std::u8string>* sound_files_u8_names_ptr;
    size_t selected_sound_file_index = data::invalid_index;

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

    std::vector<data::node_id_t> moved_nodes;
    std::vector<data::vec2_t> moved_nodes_positions;
};

view_action_type_e build_view(view_state_t& mut_view_state, const data::data_state_t& data_state);

}
}

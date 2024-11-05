#include "app_state.h"
#include "app_logic.h"
#include "app_view.h"
#include "file_data_provider.h"
#include "data_state.h"

#include "hlea/runtime.h"
#include "editor/editor_runtime.h"

#include <filesystem>
#include <algorithm>
#include <cassert>

using hle_audio::data::output_bus_t;
using hle_audio::data::file_flow_node_t;
using hle_audio::data::random_flow_node_t;
using hle_audio::data::flow_node_type_t;

namespace fs = std::filesystem;

namespace hle_audio {
namespace editor {

struct app_state_t {
    // view state is mutable within widget processing pass
    view_state_t view_state;

    std::string data_file_path;
    std::string sounds_path;

    // sounds file list
    std::vector<fs::path> sound_files;
    std::vector<std::u8string> sound_files_u8_names;

    // player context
    hlea_context_t* runtime_ctx;
    rt::editor_runtime_t* editor_runtime;
    hlea_event_bank_t* bank = nullptr;
    size_t bank_cmd_index = 0;

    std::vector<hlea_group_info_t> active_group_infos;

    logic_state_t bl_state;
    size_t save_cmd_index = 0;
};

static void update_active_group(app_state_t* state, size_t selected_group) {
    auto& view_state = state->view_state;

    view_state.active_group_index = selected_group;

    if (selected_group == data::invalid_index) return;


    view_state.selected_group_state = data::get_group(&state->bl_state.data_state, selected_group);
    ++view_state.selected_group_state_revison;
}

static void update_active_event(app_state_t* state, size_t active_index) {
    auto& view_state = state->view_state;

    view_state.active_event_index = active_index;
    
    if (active_index == data::invalid_index) return;

    auto& event = state->bl_state.data_state.events[active_index];
    view_state.event_state = event;
}

static bool find_substring_ic(std::string_view str, std::string_view substr)
{
    return std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { 
            return std::toupper(ch1) == std::toupper(ch2); 
        }
    ) != str.end();
}

static void filter_groups(app_state_t* state) {
    auto& view_state = state->view_state;

    auto& filtered_state = view_state.group_filtered_state;

    filtered_state.indices.clear();

    // exit early if no filter
    if (view_state.group_filter_str.size() == 0) {
        filtered_state.list_index = view_state.active_group_index;
        return;
    }
    filtered_state.list_index = data::invalid_index;

    const auto& groups = state->bl_state.data_state.groups;
    for (size_t group_index = 0; group_index < groups.size(); ++group_index) {
        auto& group = groups[group_index];

        // skip if name doesn't match
        if (!find_substring_ic(group.name, view_state.group_filter_str)) continue;

        filtered_state.indices.push_back(group_index);

        if (view_state.active_group_index == group_index) {
            filtered_state.list_index = filtered_state.indices.size() - 1;
        }
    }
}

static void filter_events(app_state_t* state) {
    auto& view_state = state->view_state;

    auto& filtered_state = view_state.events_filtered_state;
    filtered_state.indices.clear();

    // exit early if no filter
    if (view_state.event_filter_str.size() == 0 &&
        view_state.event_filter_group_index == data::invalid_index) {
        filtered_state.list_index = view_state.active_event_index;
        return;
    }

    filtered_state.list_index = data::invalid_index;

    const auto& events = state->bl_state.data_state.events;
    for (size_t event_index = 0; event_index < events.size(); ++event_index) {
        auto& event = events[event_index];

        // skip if name doesn't match
        if (!find_substring_ic(event.name, view_state.event_filter_str)) continue;

        // filter group
        if (view_state.event_filter_group_index != data::invalid_index &&
            !is_event_target_group(event, view_state.event_filter_group_index)) continue;

        filtered_state.indices.push_back(event_index);

        if (view_state.active_event_index == event_index) {
            filtered_state.list_index = filtered_state.indices.size() - 1;
        }
    }
}

static void filter_files(app_state_t* state) {
    auto& view_state = state->view_state;

    auto& filtered_state = view_state.files_filtered_state;
    filtered_state.indices.clear();

    // exit early if no filter
    if (view_state.files_filter_str.size() == 0) {
        filtered_state.list_index = view_state.selected_sound_file_index;
        return;
    }

    filtered_state.list_index = data::invalid_index;

    const auto& files = *view_state.sound_files_u8_names_ptr;
    for (size_t it_index = 0; it_index < files.size(); ++it_index) {
        auto& filename = files[it_index];

        std::string filename_str(filename.cbegin(), filename.cend());

        // skip if name doesn't match
        if (!find_substring_ic(filename_str, view_state.files_filter_str)) continue;

        filtered_state.indices.push_back(it_index);

        if (view_state.selected_sound_file_index == it_index) {
            filtered_state.list_index = filtered_state.indices.size() - 1;
        }
    }
}

static void update_mutable_view_state(app_state_t* state) {
    auto& view_state = state->view_state;
    auto& data_state = state->bl_state.data_state;

    auto groups_size = data_state.groups.size();

    // reset event group filter if groups list changed
    if (view_state.groups_size_on_event_filter_group != groups_size)
        view_state.event_filter_group_index = data::invalid_index;

    auto selected_group = view_state.active_group_index;
    if (groups_size == 0u) {
        selected_group = data::invalid_index;
    } else if (selected_group != data::invalid_index && 
        groups_size <= selected_group) {
        selected_group = groups_size - 1;
    }
    update_active_group(state, selected_group);

    // (n) with filter
    filter_groups(state);

    // todo: check index update case (when some previous events were removed)
    auto selected_event_index = view_state.active_event_index;
    if (data_state.events.size() <= selected_event_index) {
        selected_event_index = data::invalid_index;
    }
    update_active_event(state, selected_event_index);

    // (n) ? try skipping without any filter applied ?
    filter_events(state);
}

static void perform_undo(app_state_t* state) {
    if (apply_undo_chain(&state->bl_state.cmds, &state->bl_state.data_state)) {
        update_mutable_view_state(state);
    }
}

static void perform_redo(app_state_t* state) {
    if (apply_redo_chain(&state->bl_state.cmds, &state->bl_state.data_state)) {
        update_mutable_view_state(state);
    }
}

static void refresh_wav_list(app_state_t* state) {
    state->sound_files.clear();
    state->sound_files_u8_names.clear();

    for (const auto & entry : fs::directory_iterator(state->sounds_path)) {
        auto ext = entry.path().extension();
        if (ext == ".wav" || ext == ".mp3") {
            state->sound_files.push_back(entry.path());
            state->sound_files_u8_names.push_back(entry.path().filename().u8string());
        }
    }
}

static void create_context(app_state_t* state) {
    auto& data_state = state->bl_state.data_state;

    state->editor_runtime = rt::create_editor_runtime();

    hlea_context_create_info_t ctx_info = {};
    ctx_info.output_bus_count = data_state.output_buses.size();
    state->runtime_ctx = hlea_create(&ctx_info);
    bind(state->editor_runtime, state->runtime_ctx);
    
    assert(state->runtime_ctx);
}

static void unload_and_destroy_context(app_state_t* state) {
    if (state->bank) {
        hlea_unload_events_bank(state->runtime_ctx, state->bank);
        state->bank = nullptr;
    }
    rt::stop_file(state->editor_runtime);
    rt::destroy(state->editor_runtime);
    hlea_destroy(state->runtime_ctx);
    state->runtime_ctx = nullptr;
}

static void recreate_context(app_state_t* state) {
    unload_and_destroy_context(state);
    create_context(state);
}

static void add_output_bus(app_state_t* state) {
    auto& data_state = state->bl_state.data_state;

    output_bus_t bus = {};
    bus.name = "<bus name>";
    data_state.output_buses.push_back(bus);

    // todo: add cmd

    state->view_state.output_bus_volumes.push_back(100);

    recreate_context(state);
}

app_state_t* create_app_state(float scale) {
    auto res = new app_state_t;

    init(&res->bl_state);
    
    // setup view
    const view_state_t empty_vs = {};
    res->view_state = empty_vs;
    res->view_state.scale = scale;
    res->view_state.root_pane_width_scaled = 200 * scale;
    res->view_state.files_pane_width_scaled = 200 * scale;

    return res;
}

void destroy(app_state_t* state) {
    unload_and_destroy_context(state);
    delete state;
}

void init_with_data(app_state_t* state, const char* filepath, const char* sound_folder) {
    state->data_file_path = filepath;
    state->sounds_path = sound_folder;
    refresh_wav_list(state);

    load_store_json(&state->bl_state.data_state, state->data_file_path.c_str());

    // update view output buses volumes
    state->view_state.output_bus_volumes.clear();
    state->view_state.output_bus_volumes.insert(
        state->view_state.output_bus_volumes.end(), 
        state->bl_state.data_state.output_buses.size(), 100);

    create_context(state);

    rt::set_sounds_path(state->editor_runtime, state->sounds_path.c_str());

    update_mutable_view_state(state);
}

class editor_file_data_provider_wrapper_t : public hle_audio::data::audio_file_data_provider_ti {
public:
    rt::editor_runtime_t* editor_rt;
    data::file_data_provider_t fd_prov = {};

    data::audio_file_data_t get_file_data(const char* filename, uint32_t file_index) override {
        auto res = fd_prov.get_file_data(filename, file_index);

        cache_audio_file_data(editor_rt, filename, file_index, res.data_chunk_range);

        return res;
    }
};

static void fire_event(app_state_t* state) {
    if (state->bank && state->bank_cmd_index != get_undo_size(&state->bl_state.cmds)) {
        hlea_unload_events_bank(state->runtime_ctx, state->bank);
        state->bank = nullptr;

        rt::drop_file_cache(state->editor_runtime);
    }
    if (!state->bank) {
        // todo: this could take a while (move to async)
        editor_file_data_provider_wrapper_t ed_fd_prov = {};
        ed_fd_prov.editor_rt = state->editor_runtime;
        ed_fd_prov.fd_prov.sounds_path = state->sounds_path.c_str();
        ed_fd_prov.fd_prov.use_oggs = true;
        auto bank_buffer = save_store_blob_buffer(&state->bl_state.data_state, &ed_fd_prov);
        state->bank = hlea_load_events_bank_from_buffer(state->runtime_ctx, bank_buffer.data(), bank_buffer.size());
        state->bank_cmd_index = get_undo_size(&state->bl_state.cmds);
    }

    auto& event_name = state->bl_state.data_state.events[state->view_state.active_event_index].name;
    hlea_fire_event(state->runtime_ctx, state->bank, event_name.c_str(), 0u);
}

static void update_group_for_selected_event(app_state_t* state) {
    auto& view_state = state->view_state;

    auto& event = state->bl_state.data_state.events[view_state.active_event_index];
    auto new_group_index = view_state.active_group_index;
    for (auto& action : event.actions) {
        if (!data::is_action_target_group(action.type)) continue;

        if (action.target_index == view_state.active_group_index) {
            // keep active if it's in the action list
            new_group_index = view_state.active_group_index;
            break;
        } else if (new_group_index == view_state.active_group_index) {
            // update to first one
            new_group_index = action.target_index;
        }
    }
    if (new_group_index != view_state.active_group_index) {
        update_active_group(state, new_group_index);
        filter_groups(state);
        state->view_state.focus_selected_group = true;
    }
}

bool process_frame(app_state_t* state) {
    /**
     *  update runtime
     */
    hlea_process_frame(state->runtime_ctx);
    auto group_count = hlea_get_active_groups_count(state->runtime_ctx);
    state->active_group_infos.resize(group_count);
    hlea_get_active_groups_infos(state->runtime_ctx, state->active_group_infos.data(), group_count);

    /**
     * process view
     */
    auto& view_state = state->view_state;

    // map active_group_infos to view_state data
    view_state.active_group_infos.clear();
    for (auto& info : state->active_group_infos) {
        view_state_t::group_info_t v_info = {};
        v_info.group_index = info.group_index;
        v_info.paused = info.paused;
        view_state.active_group_infos.push_back(v_info);
    }

    //
    // build up view
    //
    view_state.has_save = state->save_cmd_index != get_undo_size(&state->bl_state.cmds);
    view_state.has_undo = has_undo(&state->bl_state.cmds);
    view_state.has_redo = has_redo(&state->bl_state.cmds);
    view_state.has_wav_playing = rt::is_file_playing(state->editor_runtime);
    view_state.sound_files_u8_names_ptr = &state->sound_files_u8_names;

    const auto prev_group_index = view_state.group_filtered_state.list_index;
    const auto prev_event_index = view_state.events_filtered_state.list_index;

    auto action = build_view(view_state, state->bl_state.data_state);

    //
    // modify view state
    //
    if (prev_group_index != view_state.group_filtered_state.list_index) {
        auto selected_group_index = view_state.group_filtered_state.get_index(view_state.group_filtered_state.list_index);
        update_active_group(state, selected_group_index);
    }

    if (prev_event_index != view_state.events_filtered_state.list_index) {
        auto selected_event_index = view_state.events_filtered_state.get_index(view_state.events_filtered_state.list_index);
        update_active_event(state, selected_event_index);

        if (view_state.option_select_group_for_event) {
            update_group_for_selected_event(state);
        }
    }


    const float runtime_fade_time = 0.3f;

    bool keep_running = true;

    //
    // modify data state
    //
    const auto& event_state = view_state.event_state;
    auto bl_state = &state->bl_state;
    switch (action)
    {
    case view_action_type_e::SAVE: {
        save_store_json(&bl_state->data_state, state->data_file_path.c_str());
        state->save_cmd_index = get_undo_size(&state->bl_state.cmds);
        break;
    }
    case view_action_type_e::UNDO:
        perform_undo(state);
        break;
    case view_action_type_e::REDO:
        perform_redo(state);
        break;

    case view_action_type_e::SWITCH_OPTION_SELECT_GROUP_FOR_EVENT:
        view_state.option_select_group_for_event = !view_state.option_select_group_for_event;
        break;

    case view_action_type_e::SAVE_AND_EXIT: // SAVE + EXIT duplication
        save_store_json(&bl_state->data_state, state->data_file_path.c_str());
        keep_running = false;
        break;
    case view_action_type_e::EXIT:
        keep_running = false;
        break;

    case view_action_type_e::GROUP_ADD: {
        auto new_group_index = view_state.action_group_index + 1;
        create_group(bl_state, new_group_index); // todo support create before/after
        update_active_group(state, new_group_index);
        // full update as group index could be updated in event actions
        update_mutable_view_state(state);
        view_state.apply_edit_focus_on_group = true;
        break;
    }
    case view_action_type_e::GROUP_REMOVE:
        remove_group(bl_state, view_state.action_group_index);
        // full update as event list could be updated
        update_mutable_view_state(state);
        break;
    case view_action_type_e::GROUP_SHOW:
        update_active_group(state, view_state.action_group_index);
        filter_groups(state);
        state->view_state.focus_selected_group = true;
        break;
    case view_action_type_e::APPLY_SELECTED_GROUP_UPDATE: {
        apply_group_update(bl_state, view_state.active_group_index, 
                view_state.selected_group_state);
        update_mutable_view_state(state);
        break;
    }

    case view_action_type_e::EVENT_ADD: {
        auto new_index = view_state.active_event_index + 1;
        create_event(bl_state, new_index);
        update_active_event(state, new_index);
        update_mutable_view_state(state);
        view_state.apply_edit_focus_on_event = true;
        break;
    }
    case view_action_type_e::EVENT_REMOVE:
        remove_event(bl_state, view_state.active_event_index);
        update_mutable_view_state(state);
        break;
    case view_action_type_e::EVENT_UPDATE:
        update_event(bl_state, view_state.active_event_index, event_state);
        update_mutable_view_state(state);
        break;
    case view_action_type_e::EVENT_ADD_ACTION: {
        rt::action_t action = {};
        if (view_state.active_group_index != data::invalid_index) {
            action.type = rt::action_type_e::play_single;
            action.target_index = view_state.active_group_index;
        }
        add_event_action(bl_state, 
            view_state.active_event_index, action);
        update_active_event(state, view_state.active_event_index);
        break;
    }
    case view_action_type_e::EVENT_REMOVE_ACTION:
        remove_event_action(bl_state, 
            view_state.active_event_index, view_state.event_action_cmd_index);
        update_active_event(state, view_state.active_event_index);
        break;

    case view_action_type_e::GROUP_FILTER:
        filter_groups(state);
        break;

    case view_action_type_e::EVENT_FILTER:
        filter_events(state);
        state->view_state.focus_selected_event = true;
        break;

    case view_action_type_e::EVENT_ADD_WITH_PLAY_GROUP: {
        auto new_index = bl_state->data_state.events.size();
        create_event(bl_state, new_index);
        auto new_event = bl_state->data_state.events[new_index];
        new_event.name = view_state.selected_group_state.name;
        rt::action_t action = {};
        action.type = rt::action_type_e::play_single;
        action.target_index = view_state.action_group_index;
        new_event.actions.push_back(action);
        update_event(bl_state, new_index, new_event);
     
        update_active_event(state, new_index);
        update_mutable_view_state(state);
        view_state.apply_edit_focus_on_event = true;
        break;
    }
    case view_action_type_e::NODE_ADD: {
        auto& node_action = view_state.node_action;

        auto add_node_type = std::get<flow_node_type_t>(node_action.action_data);
        assert(add_node_type != data::invalid_node_type);

        create_node(bl_state, view_state.active_group_index, add_node_type, node_action.add_position);

        update_active_group(state, view_state.active_group_index);

        break;
    }
    case view_action_type_e::NODE_UPDATE: {
        auto& node_action = view_state.node_action;
        auto type = bl_state->data_state.fnodes[node_action.node_id].type;
        switch (type)
        {
        case data::RANDOM_FNODE_TYPE:
            update_random_node(bl_state, node_action.node_id, 
                    std::get<random_flow_node_t>(node_action.action_data));
            break;
        case data::FILE_FNODE_TYPE:
            update_file_node(bl_state, node_action.node_id, 
                    std::get<file_flow_node_t>(node_action.action_data));
        default:
            break;
        }
        break;
    }
    case view_action_type_e::NODE_FILE_ASSIGN_SOUND: {
        auto& node_action = view_state.node_action;

        auto type = bl_state->data_state.fnodes[node_action.node_id].type;
        assert(type == data::FILE_FNODE_TYPE);
        
        auto file_list_index = view_state.selected_sound_file_index;
        if (file_list_index != data::invalid_index) {
            const auto& filename = state->sound_files_u8_names[file_list_index];
            assign_file_node_file(bl_state, node_action.node_id, filename);
        }
            
        break;
    }
    case view_action_type_e::NODE_REMOVE: {
        remove_nodes(bl_state, view_state.active_group_index, view_state.moved_nodes);

        update_active_group(state, view_state.active_group_index);

        break;
    }
    case view_action_type_e::NODE_MOVED: {
        move_nodes(bl_state, view_state.moved_nodes, view_state.moved_nodes_positions);

        break;
    }
    case view_action_type_e::REFRESH_SOUND_LIST:
        refresh_wav_list(state);
        break;
    case view_action_type_e::FILES_FILTER:
        filter_files(state);
        break;

    case view_action_type_e::BUS_ADD:
        add_output_bus(state);
        break;
    case view_action_type_e::BUS_RENAME:
        rename_bus(bl_state, 
            view_state.action_bus_index,
            view_state.bus_edit_state.name.c_str()); 
        break;

    case view_action_type_e::BUS_VOLUME_CHANGED: {
        uint8_t index = 0;
        for (auto volume_percents : state->view_state.output_bus_volumes) {
            hlea_set_bus_volume(state->runtime_ctx, index, (float)volume_percents / 100);
            ++index;
        }
        break;
    }

    case view_action_type_e::SOUND_PLAY: {
        auto file_index = view_state.selected_sound_file_index;
        auto full_path_str = state->sound_files[file_index].u8string();
        rt::play_file(state->editor_runtime, (const char*)full_path_str.c_str());
        break;
    }
    case view_action_type_e::SOUND_STOP:
        rt::stop_file(state->editor_runtime);
        break;
    case view_action_type_e::RUNTIME_FIRE_EVENT: {
        fire_event(state);
        break;
    }

    case view_action_type_e::RUNTIME_FIRE_GROUP_STOP: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::stop;
        action_info.target_index = view_state.runtime_target_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_STOP_ALL: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::stop_all;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_STOP_BUS: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::stop_bus;
        action_info.target_index = view_state.action_bus_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_PAUSE: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::pause;
        action_info.target_index = view_state.runtime_target_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_RESUME: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::resume;
        action_info.target_index = view_state.runtime_target_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_PAUSE_BUS: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::pause_bus;
        action_info.target_index = view_state.action_bus_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }
    case view_action_type_e::RUNTIME_FIRE_GROUP_RESUME_BUS: {
        hlea_action_info_t action_info = {};
        action_info.type = hlea_action_type_e::resume_bus;
        action_info.target_index = view_state.action_bus_index;
        action_info.fade_time = runtime_fade_time;
        hlea_fire_event_info_t ev_info = {};
        ev_info.bank = state->bank;
        ev_info.actions = &action_info;
        ev_info.action_count = 1;
        hlea_fire_event(state->runtime_ctx, &ev_info);
        break;
    }

    case view_action_type_e::NONE:
        break;
    default:
        assert(false && "unhandled action");
        break;
    }

    return keep_running;
}

bool request_exit(app_state_t* state) {
    if (state->view_state.has_save) {
        state->view_state.show_exit_save_dialog = true;

        return false;
    } else {
        return true;
    }
}

}
}

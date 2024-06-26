#include "app_view.h"
#include "data_state.h"
#include "imgui.h"
#include "imnodes.h"
#include "imgui_ext.h"
#include "nodes_view.h"
#include "attribute_id_utils.inl"


using hle_audio::data::data_state_t;
using hle_audio::data::pin_counts_t;
using hle_audio::data::node_id_t;
using hle_audio::data::link_t;

namespace hle_audio {
namespace editor {

static bool TreeNodeWithRemoveButton(uint32_t node_index, const char* label, bool* remove_pressed) {
    bool expanded = ImGui::TreeNodeEx((void*)(intptr_t)node_index, ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen, label);
    ImGui::SameLine();
    ImGui::PushID(expanded ? -1 - node_index : node_index); // fix id conflict with expanded node and its unexpanded children
    if (ImGui::SmallButton("-")) {
        *remove_pressed = true;
    }
    ImGui::PopID();

    return expanded;
}

static view_action_type_e process_view_menu(const view_state_t& view_state) {
    static bool show_app_metrics = false;
    static bool show_demo_window = false;

    view_action_type_e action = view_action_type_e::NONE;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // if (ImGui::MenuItem("New", "CTRL+N")) {}
            // if (ImGui::MenuItem("Open...", "CTRL+O")) {}
            if (ImGui::MenuItem("Save", "CTRL+S", false, view_state.has_save)) {
                action = view_action_type_e::SAVE;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "CTRL+Z", false, view_state.has_undo)) {
                action = view_action_type_e::UNDO;
            }
            if (ImGui::MenuItem("Redo", "CTRL+Y", false, view_state.has_redo)) {
                action = view_action_type_e::REDO;
            }
            // ImGui::Separator();
            // if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            // if (ImGui::MenuItem("Copy", "CTRL+C")) {}
            // if (ImGui::MenuItem("Paste", "CTRL+V")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Show UI metrics")) show_app_metrics = true;
            if (ImGui::MenuItem("Show demos")) {
                show_demo_window = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    if (show_app_metrics) {
        ImGui::ShowMetricsWindow(&show_app_metrics);
    }

    return action;
}

typedef const char* (*get_text_at_index_cb)(const void* ud, int index);

static void ClippedListWithAddRemoveButtons(size_t elem_count, float scale, bool force_display_selected,
                size_t selected_index, const void* ud, get_text_at_index_cb get_text_at_index,
                size_t* new_selected_index, bool* add_pressed, bool* remove_pressed, bool* double_clicked = nullptr) {
    assert(new_selected_index);
    assert(add_pressed);
    assert(remove_pressed);

    ImGuiListClipper clipper;
    clipper.Begin((int)elem_count);

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            ImGui::PushID(i);
            auto name = get_text_at_index(ud, i);
            if (ImGui::Selectable(name, selected_index == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                *new_selected_index = i;
                if (ImGui::IsMouseDoubleClicked(0) && double_clicked) {
                    * double_clicked = true;
                }
            }

            if (selected_index == i) {
                ImGui::SetItemAllowOverlap();

                auto content_width_with_scroll = ImGui::GetContentRegionMax().x - 2 * ImGui::GetWindowContentRegionMin().x;
                ImGui::SameLine(content_width_with_scroll - 30 * scale);
                if (ImGui::SmallButton("-")) {
                    *remove_pressed = true;
                }
                ImGui::SameLine(content_width_with_scroll - 15 * scale);
                if (ImGui::SmallButton("+")) {
                    *add_pressed = true;
                }
            }
            ImGui::PopID();
        }
    }

    if (force_display_selected) {
        float item_pos_y = clipper.StartPosY + clipper.ItemsHeight * selected_index;
        ImGui::SetScrollFromPosY(item_pos_y - ImGui::GetWindowPos().y);
    }
}

static void ClippedListWithAddRemoveButtonsFiltered(size_t elem_count, float scale, bool force_display_selected,
                view_state_t::filtered_indices_list_state_t& filtered_state,
                const void* ud, get_text_at_index_cb get_text_at_index,
                bool* add_pressed, bool* remove_pressed, bool* double_clicked = nullptr) {

    struct clipper_ctx_t {
        view_state_t::filtered_indices_list_state_t& filtered_state;
        const void* ud;
        get_text_at_index_cb get_text_at_index;
    };
    clipper_ctx_t ctx = {filtered_state, ud, get_text_at_index};

    auto list_size = (0 < filtered_state.indices.size()) ? 
            filtered_state.indices.size() :
            elem_count;
    ClippedListWithAddRemoveButtons(
        list_size, 
        scale, 
        force_display_selected, filtered_state.list_index, 
        &ctx, [](const void* ud, int index) {
            auto ctx_ptr = (clipper_ctx_t*)ud;

            auto event_index = ctx_ptr->filtered_state.get_index(index);
            return ctx_ptr->get_text_at_index(ctx_ptr->ud, event_index);
        },
        &filtered_state.list_index, 
        add_pressed, remove_pressed, double_clicked);
}

static uint16_t get_node_in_pin_count(const data_state_t& data_state, size_t node_index) {
    return 1;
}

static link_t attributes_to_link(const data_state_t& data_state, int start_attr, int end_attr) {
    uint32_t start_node = start_attr & 0xFFFF;
    uint32_t start_pin = uint32_t(start_attr) >> 16;
    uint32_t end_node = end_attr & 0xFFFF;
    uint32_t end_pin = uint32_t(end_attr) >> 16;

    auto in_pin_count = get_node_in_pin_count(data_state, start_node);
    start_pin -= in_pin_count;

    link_t res = {};
    res.from = node_id_t(start_node);
    res.from_pin = start_pin;
    res.to = node_id_t(end_node);
    res.to_pin = end_pin;
    return res;
}

static void group_add_link_unique(data::named_group_t& group, const link_t& link) {
    // keep single out link
    for (size_t i = 0; i < group.links.size(); ++i) {
        auto& it_l = group.links[i];
        if (it_l.from == link.from && 
                it_l.from_pin == link.from_pin) {
            group.links[i] = link;
            return;
        }
    }
    // not found existing link to replace, so add new one
    group.links.push_back(link);
}

static data::vec2_t to_grid_position(ImVec2 pos) {
    auto grid_spacing = ImNodes::GetStyle().GridSpacing;
    pos.x /= grid_spacing;
    pos.y /= grid_spacing;

    return {
        int16_t(pos.x),
        int16_t(pos.y)
    };
}

static void build_selected_group_view(view_state_t& mut_view_state, const data_state_t& data_state,
                view_action_type_e& action) {
    auto& data_group = get_group(&data_state, mut_view_state.active_group_index);

    auto& group_state = mut_view_state.selected_group_state;

    ImGuiExt::InputText("name", nullptr, &group_state.name, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivatedAfterEdit() &&
            data_group.name != group_state.name) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }
    ImGui::SliderFloat("volume", &group_state.volume, 0.0f, 1.0f);
    if (ImGui::IsItemDeactivatedAfterEdit() &&
            data_group.volume != group_state.volume) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    const char* time_sec_format = "%.3f";
    const float f32_zero = 0.0f;
    ImGui::DragScalar("cross fade time", ImGuiDataType_Float, 
            &group_state.cross_fade_time, 
            0.01f,  &f32_zero, nullptr,
            time_sec_format);
    if (ImGui::IsItemDeactivatedAfterEdit() &&
            data_group.cross_fade_time != group_state.cross_fade_time) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    int current_index = group_state.output_bus_index;
    auto getter = [](void* data, int n, const char** out_str) {
        auto buses = (decltype(&data_state.output_buses))data;
        *out_str = buses->at(n).name.c_str();
        return true;
    };
    auto data = (void*)&data_state.output_buses;
    if (ImGui::Combo("output bus", &current_index, 
            getter, data, (int)data_state.output_buses.size())) {
        group_state.output_bus_index = current_index;
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    ImGui::Text("Node graph:");
    ImGui::SameLine();
    if (ImGui::SmallButton("<<< Filter events")) {
        mut_view_state.event_filter_group_index = mut_view_state.action_group_index;
        mut_view_state.groups_size_on_event_filter_group = data_state.groups.size();
        mut_view_state.select_events_tab = true;
        action = view_action_type_e::EVENT_FILTER;
    }
    auto editor_panning = ImNodes::EditorContextGetPanning();
    if (editor_panning.x != 0.0f || editor_panning.y != 0.0f) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset panning")) {
            ImNodes::EditorContextResetPanning({});
        }
    }
    auto selected_count = ImNodes::NumSelectedNodes();
    if (selected_count) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove selected node")) {
            mut_view_state.moved_nodes.resize(selected_count);
            static_assert(sizeof(data::node_id_t) == sizeof(int));
            ImNodes::GetSelectedNodes((int*)(mut_view_state.moved_nodes.data()));
            ImNodes::ClearNodeSelection();

            action = view_action_type_e::NODE_REMOVE;
        }
    }
    if (ImNodes::NumSelectedNodes() == 1) {
        data::node_id_t selected_node_id = {};
        ImNodes::GetSelectedNodes((int*)&selected_node_id);

        if (selected_node_id != group_state.start_node) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Make start")) {
                group_state.start_node = selected_node_id;
                action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
            }
        }
    }

    ImGui::BeginChild("Node editor");
    ImNodes::BeginNodeEditor();

    const bool open_popup = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                            ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    auto editor_screen_pos = ImGui::GetCursorScreenPos();
    
    static size_t prev_group_state_revison = {};
    if (prev_group_state_revison != mut_view_state.selected_group_state_revison) {
        prev_group_state_revison = mut_view_state.selected_group_state_revison;

        auto grid_spacing = ImNodes::GetStyle().GridSpacing;
        for (auto node_index : group_state.nodes) {
            auto& node = data_state.fnodes[node_index];
            
            ImVec2 pos = {node.position.x * grid_spacing, node.position.y * grid_spacing};
            ImNodes::SetNodeGridSpacePos(node_index, pos);
        }
    }

    for (auto& node_id : group_state.nodes) {
        auto& node = data_state.fnodes[node_id];

        bool is_start_node = (node_id == group_state.start_node);

        ImNodes::BeginNode(node_id);

        build_node_view_desc_t desc = {};
        desc.node_id = node_id;
        desc.start_node = is_start_node;
        desc.out_data = &mut_view_state.node_action;

        view_action_type_e node_action = {};
        if (node.type == data::FILE_FNODE_TYPE) {
            node_action = build_node_view(get_file_node(&data_state, node_id), desc);
        } else if (node.type == data::RANDOM_FNODE_TYPE) {
            node_action = build_node_view(get_random_node(&data_state, node_id), desc);
        }

        if (node_action != view_action_type_e::NONE) {
            action = node_action;

            auto& out_action  = mut_view_state.node_action;
            out_action.node_id = node_id;
        }

        ImNodes::EndNode();
    }
    for (int i = 0; i < group_state.links.size(); ++i) {
        auto& link = group_state.links[i];

        auto in_pin_count = get_node_in_pin_count(data_state, link.from);
        ImNodes::Link(i, 
            to_attribute_id_out(in_pin_count, link.from, link.from_pin), 
            to_attribute_id_in(link.to, link.to_pin));
    }

    ImNodes::EndNodeEditor();

    int start_attr, end_attr;
    if (ImNodes::IsLinkCreated(&start_attr, &end_attr)) {
        auto link = attributes_to_link(data_state, start_attr, end_attr);
        group_add_link_unique(group_state, link);
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    // bool linkStarted = false;
    // if (ImNodes::IsLinkStarted(&start_attr)) {
    //     linkStarted = true;
    // }

    int link_id;
    if (ImNodes::IsLinkDestroyed(&link_id)) {
        group_state.links.erase(group_state.links.begin() + link_id);
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    // todo: add node with drop link
    // if (ImNodes::IsLinkDropped(&start_attr, false)) {
    //     static int qwe = 0;
    //     qwe = start_attr;
    // }

    if (ImNodes::IsNodesDragStopped()) {
        auto moved_count = ImNodes::NumSelectedNodes();
        if (moved_count) {
            mut_view_state.moved_nodes.resize(moved_count);
            static_assert(sizeof(data::node_id_t) == sizeof(int));
            ImNodes::GetSelectedNodes((int*)(mut_view_state.moved_nodes.data()));

            auto fisrst_moved_node = mut_view_state.moved_nodes[0];

            auto new_node_pos = to_grid_position(ImNodes::GetNodeGridSpacePos(fisrst_moved_node));
            
            auto& node = data_state.fnodes[fisrst_moved_node];
            if (node.position.x != new_node_pos.x ||
                    node.position.y != new_node_pos.y) {
                mut_view_state.moved_nodes_positions.clear();
                for (auto node_id : mut_view_state.moved_nodes) {
                    auto new_pos = to_grid_position(ImNodes::GetNodeGridSpacePos(node_id));
                    mut_view_state.moved_nodes_positions.push_back(new_pos);
                }
                action = view_action_type_e::NODE_MOVED;
            }
        }
    }

    static data::vec2_t click_pos = {};
    int hovered_node_id = {};
    int hovered_pin_id = {};
    if (!ImGui::IsAnyItemHovered() &&
        !ImNodes::IsNodeHovered(&hovered_node_id) && 
        !ImNodes::IsPinHovered(&hovered_pin_id) &&
        open_popup) {
        ImGui::OpenPopup("create_node_popup");

        auto mouse_pos = ImGui::GetMousePos();
        mouse_pos.x -= editor_screen_pos.x;
        mouse_pos.y -= editor_screen_pos.y;
        auto editor_panning = ImNodes::EditorContextGetPanning();
        mouse_pos.x -= editor_panning.x;
        mouse_pos.y -= editor_panning.y;

        click_pos = to_grid_position(mouse_pos);
    }

    if (ImGui::BeginPopup("create_node_popup")) {

        static const char* const node_type_names[] = {
            "File",
            "Random"
        };
        static const data::flow_node_type_t node_types[] = {
            data::FILE_FNODE_TYPE,
            data::RANDOM_FNODE_TYPE
        };
        for (int i = 0; i < std::size(node_type_names); i++) {
            if (ImGui::Selectable(node_type_names[i])) {
                action = view_action_type_e::NODE_ADD;
                mut_view_state.node_action.add_position = click_pos;
                mut_view_state.node_action.action_data = node_types[i];
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

view_action_type_e build_runtime_view(view_state_t& mut_view_state, const data_state_t& data_state) {
    view_action_type_e action = view_action_type_e::NONE;

    size_t index = 0;
    for (auto& bus : data_state.output_buses) {
        ImGui::PushID((void*)(uintptr_t)index);

        if (ImGui::Button("..")) {
            mut_view_state.action_bus_index = index;
            mut_view_state.bus_edit_state.name = bus.name;
            ImGui::OpenPopup("show_bus_popup");
        }

        if (ImGui::BeginPopup("show_bus_popup")) {
            ImGuiExt::InputText("name", nullptr, &mut_view_state.bus_edit_state.name, ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                    mut_view_state.bus_edit_state.name != bus.name) {
                action = view_action_type_e::BUS_RENAME;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::EndPopup();
        }

        ImGui::SameLine();

        int* v_ptr = &mut_view_state.output_bus_volumes[index];
        if (ImGui::SliderInt(bus.name.c_str(), 
                v_ptr, 0, 100)) {
            action = view_action_type_e::BUS_VOLUME_CHANGED;
        }

        bool has_active_groups = false;
        for (const auto& info : mut_view_state.active_group_infos) {
            auto& group_data = data_state.groups[info.group_index];
            
            if (group_data.output_bus_index == index) {
                ImGui::PushID((void*)(uintptr_t)info.group_index);

                has_active_groups = true;

                if (info.paused) {
                    ImGui::Text("%s (||)", group_data.name.c_str());
                } else {
                    ImGui::Text(group_data.name.c_str());
                }
                
                ImGui::SameLine();
                if (info.paused) {
                    if (ImGui::SmallButton("resume") ) {
                        mut_view_state.runtime_target_index = info.group_index;
                        action = view_action_type_e::RUNTIME_FIRE_GROUP_RESUME;
                    }
                } else {
                    if (ImGui::SmallButton("pause") ) {
                        mut_view_state.runtime_target_index = info.group_index;
                        action = view_action_type_e::RUNTIME_FIRE_GROUP_PAUSE;
                    }
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("stop") ) {
                    mut_view_state.runtime_target_index = info.group_index;
                    action = view_action_type_e::RUNTIME_FIRE_GROUP_STOP;
                }

                ImGui::PopID();
            }
        }
        if (has_active_groups) {
            ImGui::Text("Bus actions: ");
            ImGui::SameLine();
            if (ImGui::SmallButton("pause")) {
                mut_view_state.action_bus_index = index;
                action = view_action_type_e::RUNTIME_FIRE_GROUP_PAUSE_BUS;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("resume")) {
                mut_view_state.action_bus_index = index;
                action = view_action_type_e::RUNTIME_FIRE_GROUP_RESUME_BUS;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("stop")) {
                mut_view_state.action_bus_index = index;
                action = view_action_type_e::RUNTIME_FIRE_GROUP_STOP_BUS;
            }
        }

        ImGui::PopID();
        ImGui::Separator();
        ++index;
    }

    if (mut_view_state.active_group_infos.size()) {
        if (ImGui::SmallButton("Stop all")) {
            action = view_action_type_e::RUNTIME_FIRE_GROUP_STOP_ALL;
        }
    }

    if (ImGui::SmallButton("Add bus")) {
        action = view_action_type_e::BUS_ADD;
    }

    return action;
}

view_action_type_e build_view(view_state_t& mut_view_state, const data_state_t& data_state) {
    view_action_type_e action = process_view_menu(mut_view_state);

    float root_pane_width_max = ImGui::GetContentRegionAvail().x - mut_view_state.root_pane_width_scaled;
    ImGuiExt::Splitter(true, 4.0f, 
            &mut_view_state.root_pane_width_scaled, &root_pane_width_max, 
            50 * mut_view_state.scale, 8);

    const auto active_group_index = mut_view_state.active_group_index;
    mut_view_state.action_group_index = active_group_index;
    {
        auto padding_x = ImGui::GetStyle().WindowPadding.x;

        ImGui::BeginChild("root_pane", ImVec2(mut_view_state.root_pane_width_scaled - padding_x / 2 + 2, 0));
        if (ImGui::BeginTabBar("root_objects", ImGuiTabBarFlags_None)) {

            //
            // sound groups
            //
            if (ImGui::BeginTabItem("Groups")) {

                bool do_focus_group = mut_view_state.focus_selected_group;
                mut_view_state.focus_selected_group = false;

                if (ImGui::Button("+")) {
                    action = view_action_type_e::GROUP_ADD;
                    mut_view_state.action_group_index = data_state.groups.size() - 1;
                    mut_view_state.focus_selected_group = true;
                }

                ImGui::Separator();

                ImGui::BeginChild("Groups_list");

                bool add_pressed = false;
                bool remove_pressed = false;
                using groups_type_t = decltype(data_state.groups);
                auto groups_size = data_state.groups.size();
                ClippedListWithAddRemoveButtons(
                    groups_size, 
                    mut_view_state.scale, 
                    do_focus_group, mut_view_state.active_group_index, 
                    &data_state.groups, [](const void* ud, int index) {
                        auto elems_ptr = (const groups_type_t*)ud;
                        return elems_ptr->at(index).name.c_str();
                    },
                    &mut_view_state.active_group_index,
                    &add_pressed, &remove_pressed);

                if (add_pressed) {
                    action = view_action_type_e::GROUP_ADD;
                }
                if (remove_pressed) {
                    action = view_action_type_e::GROUP_REMOVE;
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }
            ImGuiTabItemFlags events_tab_flags = 0;
            if (mut_view_state.select_events_tab) {
                mut_view_state.select_events_tab = false;
                events_tab_flags = ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem("Events", nullptr, events_tab_flags)) {
                if (ImGui::Button("+")) {
                    action = view_action_type_e::EVENT_ADD;
                    mut_view_state.focus_selected_event = true;
                }
                ImGui::SameLine();
                if (ImGuiExt::InputText("Filter", "enter text here", 
                        &mut_view_state.event_filter_str, ImGuiInputTextFlags_AutoSelectAll)) {
                    action = view_action_type_e::EVENT_FILTER;
                }

                // group filter
                if (mut_view_state.event_filter_group_index != data::invalid_index) {
                    auto& group = data_state.groups[mut_view_state.event_filter_group_index];
                    
                    if (ImGui::SmallButton("x")) {
                        mut_view_state.event_filter_group_index = data::invalid_index;
                        action = view_action_type_e::EVENT_FILTER;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", group.name.c_str());
                }

                ImGui::Separator();
                
                ImGui::BeginChild("Events_list");
                bool add_pressed = false;
                bool remove_pressed = false;
                bool double_clicked = false;

                bool force_display_selected = mut_view_state.focus_selected_event;
                if (mut_view_state.focus_selected_event) {
                    mut_view_state.focus_selected_event = false;
                }
            
                using data_state_pointer_t = decltype(&data_state);
                ClippedListWithAddRemoveButtonsFiltered(
                    data_state.events.size(), 
                    mut_view_state.scale, 
                    force_display_selected, mut_view_state.events_filtered_state, 
                    &data_state, [](const void* ud, int index) {
                        auto data_state_ptr = (data_state_pointer_t)ud;
                        return data_state_ptr->events[index].name.c_str();
                    },
                    &add_pressed, &remove_pressed, &double_clicked);
                
                if (add_pressed) {
                    action = view_action_type_e::EVENT_ADD;
                }
                if (remove_pressed) {
                    action = view_action_type_e::EVENT_REMOVE;
                }
                if (double_clicked) {
                    action = view_action_type_e::RUNTIME_FIRE_EVENT;
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    auto& style = ImGui::GetStyle();
    const float wav_list_width = 200 * mut_view_state.scale;

    ImGui::SameLine();
    ImGui::BeginChild("Properties pane", ImVec2(-wav_list_width - style.WindowPadding.x, 0), true);

    auto apply_edit_focus_on_event = mut_view_state.apply_edit_focus_on_event;
    mut_view_state.apply_edit_focus_on_event = false;
            
    if (mut_view_state.active_event_index != data::invalid_index) {
        if (ImGui::CollapsingHeader("Event Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (apply_edit_focus_on_event) {
                ImGui::SetKeyboardFocusHere();
            }

            auto& ds_event = data_state.events[mut_view_state.active_event_index];

            auto& event_state = mut_view_state.event_state;
            ImGuiExt::InputText("name##event_name", nullptr, &event_state.name, ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemDeactivatedAfterEdit() &&
                    event_state.name != ds_event.name) {
                action = view_action_type_e::EVENT_UPDATE;
            }

            ImGui::Text("Actions:");
            if (ImGui::BeginTable("actions", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100 * mut_view_state.scale);
                ImGui::TableSetupColumn("Target group", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Fade time", ImGuiTableColumnFlags_WidthFixed, 50 * mut_view_state.scale);
                ImGui::TableHeadersRow();

                size_t action_index = 0;
                for (auto& ev_action : event_state.actions) {
                    ImGui::TableNextRow(0, 20 * mut_view_state.scale);
                    ImGui::PushID((void*)(uintptr_t)action_index);

                    // type
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    auto current_index = (int)ev_action.type;
                    ImGui::Combo("##type", &current_index, rt::c_action_type_names, sizeof(rt::c_action_type_names) / sizeof(*rt::c_action_type_names));
                    if (current_index != (int)ev_action.type) {
                        bool prev_type_group = data::is_action_target_group(ev_action.type);

                        ev_action.type = (rt::action_type_e)current_index;

                        // reset group index if type is not group anymore
                        if (prev_type_group && !data::is_action_target_group(ev_action.type)) {
                            ev_action.target_index = 0;

                        // assign an active group if type changed to group target
                        } else if(!prev_type_group && data::is_action_target_group(ev_action.type)) {
                            if (active_group_index != data::invalid_index) {
                                ev_action.target_index = active_group_index;
                            }
                        }

                        action = view_action_type_e::EVENT_UPDATE;
                    }
                    
                    //
                    // target group
                    //
                    ImGui::TableNextColumn();

                    if (ImGui::Button("..")) {
                        mut_view_state.event_action_cmd_index = action_index;
                        ImGui::OpenPopup("show_event_action_popup");
                    }
                    if (ImGui::BeginPopup("show_event_action_popup")) {
                        if (ImGui::MenuItem("Remove action")) {
                            action = view_action_type_e::EVENT_REMOVE_ACTION;
                        }
                        // group target actions
                        if (data::is_action_target_group(ev_action.type)) {
                            if (active_group_index != data::invalid_index && // no assign if not active group
                                active_group_index != ev_action.target_index && // no assign if active group is the same
                                    ImGui::MenuItem("Assign active group")) {
                                ev_action.target_index = active_group_index;
                                action = view_action_type_e::EVENT_UPDATE;
                            }

                            if (ImGui::MenuItem("Show group")) {
                                mut_view_state.active_group_index = ev_action.target_index;
                                mut_view_state.focus_selected_group = true;
                            }
                        }
                        
                        ImGui::EndPopup();
                    }

                    ImGui::SameLine();
                    
                    // action target
                    if (data::is_action_target_group(ev_action.type)) {
                        auto& group = data_state.groups[ev_action.target_index];
                        const char* target_label = group.name.c_str();
                        ImGui::Text(target_label);
                    } else if (data::is_action_type_target_bus(ev_action.type)) {

                        int current_index = ev_action.target_index;
                        auto getter = [](void* data, int n, const char** out_str) {
                            auto buses = (decltype(&data_state.output_buses))data;
                            *out_str = buses->at(n).name.c_str();
                            return true;
                        };
                        auto data = (void*)&data_state.output_buses;
                        if (ImGui::Combo("output bus", &current_index, 
                                getter, data, (int)data_state.output_buses.size())) {
                            if (ev_action.target_index != current_index) {
                                ev_action.target_index = current_index;
                                action = view_action_type_e::EVENT_UPDATE;
                            }
                        }
                    } else if (ev_action.type == rt::action_type_e::none) {
                        ImGui::Text("none");
                    } else {
                        ImGui::Text("all groups");
                    }
                    

                    // fade time
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    const char* time_sec_format = "%.3f";

                    const float f32_zero = 0.0f;
                    ImGui::DragScalar("##fade_time", ImGuiDataType_Float, 
                            &ev_action.fade_time, 
                            0.01f,  &f32_zero, nullptr,
                            time_sec_format);
                    if (ImGui::IsItemDeactivatedAfterEdit() &&
                            ev_action.fade_time != ds_event.actions[action_index].fade_time) {
                        action = view_action_type_e::EVENT_UPDATE;
                    }

                    ImGui::PopID();
                    ++action_index;
                }

                ImGui::EndTable();
            }

            if (ImGui::Button("Add")) {
                action = view_action_type_e::EVENT_ADD_ACTION;
            }

            ImGui::Separator();
            if (ImGui::Button("Fire")) {
                action = view_action_type_e::RUNTIME_FIRE_EVENT;
            }
        }
    }

    auto apply_edit_focus_on_group = mut_view_state.apply_edit_focus_on_group;
    mut_view_state.apply_edit_focus_on_group = false;
    if (active_group_index != data::invalid_index) {
        if (ImGui::CollapsingHeader("Group Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (apply_edit_focus_on_group) {
                ImGui::SetKeyboardFocusHere();
            }

            build_selected_group_view(mut_view_state, data_state, action);
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // bus edit data
    ImGui::BeginChild("right_pane", ImVec2(wav_list_width, 0));
    if (ImGui::CollapsingHeader("Runtime", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto rt_action = build_runtime_view(mut_view_state, data_state);
        if (rt_action != view_action_type_e::NONE) {
            action = rt_action;
        }
        ImGui::Separator();
    }

    //
    // Sound file list
    //
    {
        auto& file_list = *mut_view_state.sound_files_u8_names_ptr;

        ImGui::BeginGroup();
        
        ImGui::Text("Sound files (%d):", (int)file_list.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            action = view_action_type_e::REFRESH_SOUND_LIST;
        }
        ImGui::BeginDisabled(mut_view_state.selected_sound_file_index == data::invalid_index);
        if (ImGui::SmallButton("Play")) {
            action = view_action_type_e::SOUND_PLAY;
        }
        if (mut_view_state.has_wav_playing) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) {
                action = view_action_type_e::SOUND_STOP;
            }
        }
        ImGui::EndDisabled();

        ImGui::BeginChild("Files", ImVec2(wav_list_width, 0), true);
        ImGuiListClipper clipper;
        clipper.Begin((int)file_list.size());
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                auto& filename = file_list[i];
                if (ImGui::Selectable((const char*)filename.c_str(), mut_view_state.selected_sound_file_index == i))
                    mut_view_state.selected_sound_file_index = i; 
                if (mut_view_state.selected_sound_file_index == i) {
                    ImGui::SetItemAllowOverlap();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Play")) {
                        action = view_action_type_e::SOUND_PLAY;
                    }
                    if (mut_view_state.has_wav_playing) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Stop")) {
                            action = view_action_type_e::SOUND_STOP;
                        }
                    }
                }
            }
        ImGui::EndChild();

        ImGui::EndGroup();
    }   

    ImGui::EndChild(); // right pane

    //
    // Exit save dialog
    //
    if (mut_view_state.show_exit_save_dialog) {
        mut_view_state.show_exit_save_dialog = false;

        ImGui::OpenPopup("Save?");
    }

    if (ImGui::BeginPopupModal("Save?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("All those beautiful files will be deleted.\nThis operation cannot be undone!\n\n");


        if (ImGui::Button("Save and Close")) { 
            ImGui::CloseCurrentPopup();

            action = view_action_type_e::SAVE_AND_EXIT;
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Close without Saving")) { 
            ImGui::CloseCurrentPopup(); 
            
            action = view_action_type_e::EXIT;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { 
            ImGui::CloseCurrentPopup(); 
        }
        ImGui::EndPopup();
    }


    // hotkyes
    // todo: unify actions with main menus
    if (!ImGui::IsAnyItemActive()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            action = view_action_type_e::SAVE;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            action = view_action_type_e::UNDO;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            action = view_action_type_e::REDO;
        }
    }

    return action;
}

}
}
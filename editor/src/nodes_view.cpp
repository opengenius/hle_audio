#include "nodes_view.h"
#include "imgui.h"
#include "imnodes.h"
#include "attribute_id_utils.inl"
#include <algorithm>
#include "imgui_utils.inl"

using hle_audio::data::pin_counts_t;

static const bool ENABLE_DEBUG_DRAW = false;

namespace hle_audio {
namespace editor {

static void TextAligned(const char* label, float avail, float alignment = 0.5f) {
    ImGuiStyle& style = ImGui::GetStyle();

    float size = ImGui::CalcTextSize(label).x;// + style.FramePadding.x * 2.0f;
    //  = ImGui::GetContentRegionAvail().x;

    float off = (avail - size) * alignment;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    ImGui::Text(label);
}

static void begin_node_title(const build_node_view_desc_t& desc) {
    ImNodes::BeginNode(desc.node_id);
    ImNodes::BeginNodeTitleBar();
    if (desc.start_node) {
        ImGui::Text("(*)");
        ImGui::SameLine();
    }
}

static void InputAttribute(data::node_id_t node_id, data::link_type_e type = data::link_type_e::EXECUTION) {
    auto shape = ImNodesPinShape_TriangleFilled;
    if (type == data::link_type_e::FILTER) {
        ImNodes::PushColorStyle(ImNodesCol_Pin, FILTER_LINK_COLOR);
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, FILTER_LINK_HOVERED_COLOR);
        shape = ImNodesPinShape_CircleFilled;
    }
    attribute_id_t attr = {node_id, 0};
    ImNodes::BeginInputAttribute(pack_attribute_id(attr), shape);
    ImNodes::EndInputAttribute();

    if (type == data::link_type_e::FILTER) {
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }
}

view_action_type_e build_node_view(const data::file_flow_node_t& file_node, const build_node_view_desc_t& desc) {
    const float node_min_width = ImNodes::GetStyle().GridSpacing * 5.0f - ImNodes::GetStyle().NodePadding.x * 2.0f;
    const pin_counts_t pin_counts = {1, 1};

    view_action_type_e action = view_action_type_e::NONE;

    begin_node_title(desc);

    auto file_name = (const char*)file_node.filename.c_str();

    // TextAligned(file_name, node_width, 0.5f);
    ImGui::Text(file_name);
    ImNodes::EndNodeTitleBar();

    auto title_rect_w = ImGui::GetItemRectSize().x;
    if (ENABLE_DEBUG_DRAW) {
        ImGui::GetForegroundDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));
    }

    // ImGui::Dummy(ImVec2(node_width, 0.f));

    auto cursor_start_x = ImGui::GetCursorPosX();
    {
        ImGui::BeginGroup();

        InputAttribute(desc.node_id);

        // auto& out_action = mut_view_state.node_action;

        // ImGui::BeginDisabled(mut_view_state.selected_sound_file_index == data::invalid_index);
        if (ImGui::SmallButton("Use selected sound file")) {
            action = view_action_type_e::NODE_FILE_ASSIGN_SOUND;
        }
        // ImGui::EndDisabled();

        ImGui::BeginDisabled(file_node.stream);
        bool loop_state = file_node.stream ? false : file_node.loop;
        if (ImGui::Checkbox("loop", &loop_state)) {
            auto file_node_copy = file_node;
            file_node_copy.loop = loop_state;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = file_node_copy;
        }
        ImGui::EndDisabled();

        bool stream_state = file_node.stream;
        if (ImGui::Checkbox("stream", &stream_state)) {
            auto file_node_copy = file_node;
            file_node_copy.stream = stream_state;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = file_node_copy;
        }
        ImGui::EndGroup();
    }
    ImGui::SameLine();

    const int in_pin_count = 1;
    const float cursor_off_x = ImGui::GetCursorPosX() - cursor_start_x;
    auto node_width = std::max(title_rect_w - ImNodes::GetStyle().NodePadding.x * 2.0f, node_min_width);
    if (ENABLE_DEBUG_DRAW) {
        ImGui::GetForegroundDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));
    }
    {
        ImGui::BeginGroup();

        attribute_id_t attr_out0 = out_to_attribute_id(in_pin_count, {desc.node_id, 0});
        ImNodes::BeginOutputAttribute(pack_attribute_id(attr_out0), ImNodesPinShape_TriangleFilled);
        TextAligned("out",  std::max(0.0f, node_width - cursor_off_x), 1.0f);
        ImNodes::EndOutputAttribute();


        ImNodes::PushColorStyle(ImNodesCol_Pin, FILTER_LINK_COLOR);
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, FILTER_LINK_HOVERED_COLOR);
        
        attribute_id_t attr_out1 = out_to_attribute_id(in_pin_count, {desc.node_id, 1});
        ImNodes::BeginOutputAttribute(pack_attribute_id(attr_out1), ImNodesPinShape_CircleFilled);
        TextAligned("filter",  std::max(0.0f, node_width - cursor_off_x), 1.0f);
        ImNodes::EndOutputAttribute();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();

        ImGui::EndGroup();
    }
    ImNodes::EndNode();

    return action;
}

view_action_type_e build_node_view(const data::random_flow_node_t& node, const build_node_view_desc_t& desc) {
    const float node_width = ImNodes::GetStyle().GridSpacing * 5.0f - ImNodes::GetStyle().NodePadding.x * 2.0f;

    view_action_type_e action = view_action_type_e::NONE;

    begin_node_title(desc);
    
    // TextAligned(title, node_width, 0.5f);
    ImGui::Text("random");
    ImNodes::EndNodeTitleBar();

    // ImGui::Dummy(ImVec2(node_width, 0.f));

    auto cursor_start_x = ImGui::GetCursorPosX();
    {
        ImGui::BeginGroup();
        InputAttribute(desc.node_id);
        ImGui::EndGroup();
    }
    ImGui::SameLine();


    const int in_pin_count = 1;
    const float cursor_off_x = ImGui::GetCursorPosX() - cursor_start_x;
    const float rest_width = node_width - cursor_off_x;
    {
        ImGui::BeginGroup();

        for (uint16_t i = 0; i < node.out_pin_count; ++i) {
            attribute_id_t attr_out = out_to_attribute_id(in_pin_count, {desc.node_id, i});
            ImNodes::BeginOutputAttribute(pack_attribute_id(attr_out), ImNodesPinShape_TriangleFilled);
            TextAligned("out", rest_width, 1.0f);
            ImNodes::EndOutputAttribute();
        }

        if (imgui_utils::SmallButtonAligned("Add pin +", rest_width, 1.0f)) {
            auto node_copy = node;
            ++node_copy.out_pin_count;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = node_copy;
        }
        ImGui::EndGroup();
    }
    ImNodes::EndNode();

    return action;
}

view_action_type_e build_node_view(const data::fade_flow_node_t& node, const build_node_view_desc_t& desc) {
    const float node_width = ImNodes::GetStyle().GridSpacing * 5.0f - ImNodes::GetStyle().NodePadding.x * 2.0f;

    view_action_type_e action = view_action_type_e::NONE;

    ImNodes::PushColorStyle(ImNodesCol_TitleBar, FILTER_LINK_COLOR);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, FILTER_LINK_HOVERED_COLOR);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, FILTER_LINK_HOVERED_COLOR);

    begin_node_title(desc);
    
    // TextAligned(title, node_width, 0.5f);
    ImGui::Text("fade");
    ImNodes::EndNodeTitleBar();

    auto cursor_start_x = ImGui::GetCursorPosX();
    {
        ImGui::BeginGroup();

        InputAttribute(desc.node_id, data::link_type_e::FILTER);

        static imgui_utils::item_state_t drag_state = {};
        float changed_value;

        ImGui::SetNextItemWidth(node_width);
        if (imgui_utils::DragFloatWithState(&drag_state, "start time", node.start_time, &changed_value) && 
                changed_value != node.start_time) {
            auto node_copy = node;
            node_copy.start_time = changed_value;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = node_copy;
        }

        ImGui::SetNextItemWidth(node_width);
        if (imgui_utils::DragFloatWithState(&drag_state, "end time", node.end_time, &changed_value) && 
                changed_value != node.end_time) {
            auto node_copy = node;
            node_copy.end_time = changed_value;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = node_copy;
        }

        ImGui::EndGroup();
    }
    ImNodes::EndNode();

    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();

    return action;
}

view_action_type_e build_node_view(const data::delay_flow_node_t& node, const build_node_view_desc_t& desc) {
    const float node_width = ImNodes::GetStyle().GridSpacing * 5.0f - ImNodes::GetStyle().NodePadding.x * 2.0f;

    view_action_type_e action = view_action_type_e::NONE;

    begin_node_title(desc);
    
    ImGui::Text("delay");
    ImNodes::EndNodeTitleBar();

    auto cursor_start_x = ImGui::GetCursorPosX();
    {
        ImGui::BeginGroup();

        InputAttribute(desc.node_id);

        static imgui_utils::item_state_t drag_state = {};
        float changed_value;

        ImGui::SetNextItemWidth(node_width);
        if (imgui_utils::DragFloatWithState(&drag_state, "time", node.time, &changed_value) && 
                changed_value != node.time) {
            auto node_copy = node;
            node_copy.time = changed_value;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = node_copy;
        }

        ImGui::EndGroup();
    }
    ImGui::SameLine();

    const int in_pin_count = 1;
    const float cursor_off_x = ImGui::GetCursorPosX() - cursor_start_x;
    const float rest_width = node_width - cursor_off_x;
    {
        ImGui::BeginGroup();

        attribute_id_t attr_out0 = out_to_attribute_id(in_pin_count, {desc.node_id, 0});
        ImNodes::BeginOutputAttribute(pack_attribute_id(attr_out0), ImNodesPinShape_TriangleFilled);
        TextAligned("out",  std::max(0.0f, rest_width), 1.0f);
        ImNodes::EndOutputAttribute();

        ImGui::EndGroup();
    }
    ImNodes::EndNode();

    return action;
}

}
}
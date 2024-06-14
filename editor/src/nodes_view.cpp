#include "nodes_view.h"
#include "imgui.h"
#include "imnodes.h"
#include "attribute_id_utils.inl"
#include <algorithm>

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

static bool SmallButtonAligned(const char* label, float avail, float alignment = 0.5f) {
    ImGuiStyle& style = ImGui::GetStyle();

    float size = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    //  = ImGui::GetContentRegionAvail().x;

    float off = (avail - size) * alignment;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    return ImGui::SmallButton(label);
}

static void begin_node_title(const build_node_view_desc_t& desc) {
    ImNodes::BeginNodeTitleBar();
    if (desc.start_node) {
        ImGui::Text("(*)");
        ImGui::SameLine();
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

    auto title_rect_w = ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x;
    if (ENABLE_DEBUG_DRAW) {
        ImGui::GetForegroundDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));
    }

    // ImGui::Dummy(ImVec2(node_width, 0.f));

    auto cursor_start_x = ImGui::GetCursorPosX();
    {
        ImGui::BeginGroup();

        ImNodes::BeginInputAttribute(to_attribute_id_in(desc.node_id, 0), ImNodesPinShape_TriangleFilled);
        ImNodes::EndInputAttribute();

        // auto& out_action = mut_view_state.node_action;

        // ImGui::BeginDisabled(mut_view_state.selected_sound_file_index == data::invalid_index);
        if (ImGui::SmallButton("Use selected sound file")) {
            action = view_action_type_e::NODE_FILE_ASSIGN_SOUND;
        }
        // ImGui::EndDisabled();

        bool loop_state = file_node.loop;
        if (ImGui::Checkbox("loop", &loop_state)) {
            auto file_node_copy = file_node;
            file_node_copy.loop = loop_state;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = file_node_copy;
        }

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

    float cursor_off_x = ImGui::GetCursorPosX() - cursor_start_x;
    auto node_width = std::max(title_rect_w - ImNodes::GetStyle().NodePadding.x * 2.0f, node_min_width);
    if (ENABLE_DEBUG_DRAW) {
        ImGui::GetForegroundDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 0, 0, 255));
    }
    {
        ImGui::BeginGroup();

        ImNodes::BeginOutputAttribute(to_attribute_id_out(1, desc.node_id, 0), ImNodesPinShape_TriangleFilled);
        TextAligned("out",  std::max(0.0f, node_width - cursor_off_x), 1.0f);
        ImNodes::EndOutputAttribute();

        ImGui::EndGroup();
    }

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

        ImNodes::BeginInputAttribute(to_attribute_id_in(desc.node_id, 0), ImNodesPinShape_TriangleFilled);
        ImNodes::EndInputAttribute();

        ImGui::EndGroup();
    }
    ImGui::SameLine();

    const float cursor_off_x = ImGui::GetCursorPosX() - cursor_start_x;
    const float rest_width = node_width - cursor_off_x;
    {
        ImGui::BeginGroup();

        for (uint16_t i = 0; i < node.out_pin_count; ++i) {
            ImNodes::BeginOutputAttribute(to_attribute_id_out(1, desc.node_id, i), ImNodesPinShape_TriangleFilled);
            TextAligned("out", rest_width, 1.0f);
            ImNodes::EndOutputAttribute();
        }

        if (SmallButtonAligned("Add pin +", rest_width, 1.0f)) {
            auto node_copy = node;
            ++node_copy.out_pin_count;

            action = view_action_type_e::NODE_UPDATE;
            desc.out_data->action_data = node_copy;
        }
        ImGui::EndGroup();
    }

    return action;
}

}
}
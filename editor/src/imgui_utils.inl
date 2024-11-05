#pragma once

#include "imgui.h"

namespace hle_audio {
namespace editor {
namespace imgui_utils {

static bool SmallButtonAligned(const char* label, float avail, float alignment = 0.5f, float* out_size = nullptr) {
    ImGuiStyle& style = ImGui::GetStyle();

    float size = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    //  = ImGui::GetContentRegionAvail().x;
    if (out_size) *out_size = size;

    float off = (avail - size) * alignment;
    if (off > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

    return ImGui::SmallButton(label);
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

}
}
}

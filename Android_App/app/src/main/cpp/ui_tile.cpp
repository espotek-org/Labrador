#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_tile.h"
void UI_tile::standard_header(float width_pixels)
{
    is_expanded = next_is_expanded;
    ImGuiStyle& style = ImGui::GetStyle();

    float close_button_width = ImGui::GetFontSize() + style.FramePadding.x;
    ImVec2 start_pos = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f,style.ItemSpacing.y)); // combined with lines in settings_panel.cpp, effectively folds ItemSpacing.y into the individual ui_tile groups
    ImGui::Text("%s",short_name);
    ImGui::SetCursorScreenPos(start_pos);
    ImGui::PushID(short_name);
//     TODO : make button larger, remove Dummy at the end
    float invisible_button_width;
    ImVec2 close_button_pos = start_pos + ImVec2(width_pixels - close_button_width,style.ItemSpacing.y);
    if(is_expanded) {
        invisible_button_width = width_pixels;
    } else {
        invisible_button_width = width_pixels - close_button_width;
    }
    float itemspacingy = style.ItemSpacing.y;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{style.ItemSpacing.x,0.f}); // fold ItemSpacing.y into the invisible button below
    if(ImGui::InvisibleButton("toggle is_expanded", {invisible_button_width, ImGui::GetFontSize() + 2 * itemspacingy}))
    {
        next_is_expanded = !next_is_expanded;
    }
    ImGui::PopID();

    if(!is_expanded)
    {
        char buf[64];
        sprintf(buf, "%s_close", short_name);
        if(ImGui::CloseButton(ImGui::GetID(buf), close_button_pos))
        {
            is_visible = false;
        }
        ImGui::SameLine();
        // mimic the spacing/cursor advancement that a standard Button(...) would generate
        ImGui::SetCursorScreenPos(close_button_pos);
        ImGui::Dummy({close_button_width,0.f});
    } 
    ImGui::PopStyleVar();
}

int UI_tile::get_collapsed_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::GetFontSize() + 2 * style.ItemSpacing.y;
}

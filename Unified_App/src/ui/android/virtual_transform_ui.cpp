#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "virtual_transform_ui.h"


void virtualTransformUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    ImGuiStyle& style = ImGui::GetStyle();

    for (int ch : {1,2}) {
        both_ch_settings[ch-1].is_paused = librador_get_paused(ch); // could have been set to true by a singleshot trigger
    }
//                 if(xy && j==2) // sync ch1 and ch2 pause states in xy mode
//                     *(checkbox_bool[j] + (i+1)%2) = *(checkbox_bool[j] + i);

    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded) {
        ImGui::EndGroup();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    ImGui::BeginGroup(); // for bounding rect
    if(ImGui::BeginTable("helper1",2, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX, ImVec2(width_pixels, 0.))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f, style.FramePadding.y - style.ItemSpacing.y));
#define ALIGN_Y ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(0.f, style.FramePadding.y - style.ItemSpacing.y));
        ImGui::Text("CH: ");
        ImGui::SameLine();
        ALIGN_Y
        ImGui::RadioButton("1  ", &ch_sel, 1);
        ImGui::SameLine();
        ALIGN_Y
        ImGui::RadioButton("2", &ch_sel, 2); 
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((ImGui::GetContentRegionAvail().x - CHECKBOX_SIZE - ImGui::CalcTextSize("||").x - style.ItemInnerSpacing.x)/2.,0.f));
        ImGui::Checkbox("||", &curr_ch_settings->is_paused);
        ImGui::EndTable();
    }

    const float offset_button_width = style.FramePadding.x*2 + ImGui::CalcTextSize("Offset").x;
    if(ImGui::BeginTable("helper2",2, ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg , ImVec2(width_pixels, 0.))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-1); 
        ImGui::custom_SliderFloat("##offset", "V", &curr_ch_settings->offset, -20.f, 20.f, "%.1f V", ImGuiSliderFlags_ClampOnInput);
        SKOIA;
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(style.CellPadding.x,0.f));
        ButtonForSlider("Offset", "##offset", ImVec2(ImGui::GetContentRegionAvail().x + style.CellPadding.x,0.f));
        ImGui::EndTable();
    }
    if(ImGui::BeginTable("helper3", 2, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH, ImVec2(width_pixels, 0.f))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
//         ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {style.FramePadding.x/4.f,style.FramePadding.y});
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - style.FramePadding.x - ImGui::CalcTextSize("Gain").x);
        ImGui::Combo("Gain", &curr_ch_settings->gain_sel, gain_labels, IM_COUNTOF(gain_labels));
//         ImGui::PopStyleVar();
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((ImGui::GetContentRegionAvail().x - CHECKBOX_SIZE - ImGui::CalcTextSize("AC").x - style.ItemInnerSpacing.x)/2.,0.f));
        ImGui::Checkbox("AC", &curr_ch_settings->is_ac);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(); //iteminnerspacing
    ImGui::EndGroup();

    curr_ch_settings = &both_ch_settings[ch_sel-1];
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255),0,0,2);
    ImGui::EndGroup();
    librador_set_virtual_transform_settings(ch_sel, 
            (o1buffer::virtual_transform_settings) 
            {.offset=curr_ch_settings->offset, .gain=gains[curr_ch_settings->gain_sel], .is_ac=curr_ch_settings->is_ac, .is_paused=curr_ch_settings->is_paused}); 
}

int virtualTransformUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    int calc_height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                      CHECKBOX_SIZE + 2 * style.CellPadding.y + \
                      2 * (ImGui::GetFontSize() + 2 * style.FramePadding.y + 2 * style.CellPadding.y);
    return calc_height;
}

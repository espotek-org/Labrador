#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "trigger_ui.h"


void triggerUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    bool enable_helper[2] = {inputs_ui->scope_enable[0] || inputs_ui->mm, inputs_ui->scope_enable[1]};
    for (int ch:{1,2})
    {
        if(!enable_helper[ch-1]) {
            both_ch_trigger_settings[ch-1] = o1buffer::trigger_settings();
        }
    }
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup(); 
    standard_header(width_pixels);
    if(!is_expanded)
    {
        ImGui::EndGroup();
        return;
    }
    ImGui::BeginGroup(); // for bounding rect
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    if(ImGui::BeginTable("trigger_helper1",1, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_NoHostExtendX, ImVec2(width_pixels, 0.))) {
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
        ImGui::EndTable();
    }
    curr_ch_trigger_settings = &both_ch_trigger_settings[ch_sel - 1];
    ImGui::BeginDisabled(!enable_helper[ch_sel-1]);
    if(ImGui::BeginTable("trigger_helper2",2, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX, ImVec2(width_pixels, 0.))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((ImGui::GetContentRegionAvail().x - CHECKBOX_SIZE - style.ItemInnerSpacing.x - ImGui::CalcTextSize("Rising").x)/2.,0.f));
        ImGui::custom_RadioButton("Rising", (int *) &curr_ch_trigger_settings->trigger_type, 1);
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((ImGui::GetContentRegionAvail().x - CHECKBOX_SIZE - style.ItemInnerSpacing.x - ImGui::CalcTextSize("Falling").x)/2.,0.f));
        ImGui::custom_RadioButton("Falling", (int *) &curr_ch_trigger_settings->trigger_type, 2);
        ImGui::EndTable();
    }
    if(ImGui::BeginTable("trigger_helper3",2, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoHostExtendX, ImVec2(width_pixels, 0.))) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.25f);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-1); 
        ImGui::custom_SliderFloat("##trigger level", "V", &curr_ch_trigger_settings->trigger_level, -20.f, 20.f, "%.1f V", ImGuiSliderFlags_ClampOnInput);
        SKOIA;
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(style.CellPadding.x,0.f));
        ButtonForSlider("Level", "##trigger level", ImVec2(ImGui::GetContentRegionAvail().x + style.CellPadding.x,0.f));
        ImGui::EndTable();
    }
    if(ImGui::BeginTable("trigger_helper4",1, ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_NoHostExtendX, ImVec2(width_pixels, 0.))) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((ImGui::GetContentRegionAvail().x - CHECKBOX_SIZE - style.ItemInnerSpacing.x - ImGui::CalcTextSize("Single shot").x)/2.,0.f));
        ImGui::Checkbox("Single shot", &curr_ch_trigger_settings->is_single_shot);
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    ImGui::PopStyleVar(); //itemspacing
    ImGui::EndGroup();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
    ImGui::EndGroup();

    if(curr_ch_trigger_settings->trigger_type != o1buffer::TriggerType::Disabled)
    {
        both_ch_trigger_settings[ch_sel%2].trigger_type = o1buffer::TriggerType::Disabled;
        librador_set_trigger_settings(ch_sel%2+1, both_ch_trigger_settings[ch_sel%2]);
    }

    librador_set_trigger_settings(ch_sel, both_ch_trigger_settings[ch_sel-1]);

}

int triggerUI::get_height() 
{
    ImGuiStyle& style = ImGui::GetStyle();
    int calc_height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                         3 * (CHECKBOX_SIZE + 2 * style.CellPadding.y) + \
                        (ImGui::GetFontSize() + 2 * style.FramePadding.y + 2 * style.CellPadding.y);
    return calc_height;
}

// singlet-width version that could become useful in the future
// void triggerUI::draw(float width_pixels, inputsUI* inputs_ui)
// {
//     bool enable_helper[2] = {inputs_ui->scope_enable[0] || inputs_ui->mm, inputs_ui->scope_enable[1]};
//     for (int ch:{1,2})
//     {
//         if(!enable_helper[ch-1]) {
//             both_ch_trigger_settings[ch-1] = o1buffer::trigger_settings();
//         }
//     }
//     ImGuiStyle& style = ImGui::GetStyle();
//     ImGui::BeginGroup(); 
//     standard_header(width_pixels);
//     if(!is_expanded)
//     {
//         ImGui::EndGroup();
//         return;
//     }
//     ImGui::BeginGroup(); // for bounding rect
//     // get_height() line 1 end
// 
//     ImGui::BeginGroup(); // for bounding rect
//         int free_space = width_pixels - ImGui::CalcTextSize("ChAChB").x - 2 * CHECKBOX_SIZE - 2 * style.ItemInnerSpacing.x - style.FramePadding.x * 2;
//         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + style.FramePadding);
//         ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(free_space,0.f));
//         ImGui::RadioButton("ChA", &ch_sel, 1); ImGui::SameLine();
//         ImGui::RadioButton("ChB", &ch_sel, 2); 
//         ImGui::PopStyleVar();
//     ImGui::EndGroup();
//     curr_ch_trigger_settings = &both_ch_trigger_settings[ch_sel - 1];
//     ImVec2 p0 = ImGui::GetItemRectMin();
//     ImVec2 p1 = ImGui::GetItemRectMax() + style.FramePadding;
//     ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p1.y)); // replacing ItemSpacing with FramePadding
//     ImVec2 slider_top_right = p1;
//     ImDrawList* draw_list = ImGui::GetWindowDrawList();
//     draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
//     // get_height() line 2 end
// 
//     float slider_left1 = (p0 + (p1-p0) * .75).x;
//     ImGui::BeginDisabled(!enable_helper[ch_sel-1]);
//     ImGui::BeginGroup();
//         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + style.FramePadding);
//         ImGui::custom_RadioButton("Rising", (int *) &curr_ch_trigger_settings->trigger_type, 1);
//         ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(style.FramePadding.x,0.f));
//         ImGui::custom_RadioButton("Falling", (int *) &curr_ch_trigger_settings->trigger_type, 2); 
//         p1 = ImGui::GetItemRectMax() + style.FramePadding;
//         float slider_left2 = p1.x;
//         float slider_left = fmax(slider_left1, slider_left2);
//         p0 = ImVec2(ImGui::GetItemRectMin().x - style.FramePadding.x, p1.y);
//         ImVec2 saved_pos = p0;
//     // get_height() line 3 end
//         draw_list = ImGui::GetWindowDrawList();
//         draw_list->AddLine(p0, p1, IM_COL32(90, 90, 120, 255));
//         float ss_text_height = ImGui::CalcTextSize("Single\n shot").y + 2 * style.ItemSpacing.y + 2; // +2 is a fudge factor that makes this correct
//         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetItemRectMin().x, p0.y + (ss_text_height - CHECKBOX_SIZE)/2.)); 
//         ImGui::Checkbox("##ss", &curr_ch_trigger_settings->is_single_shot);
//         ImGui::SameLine();
//         ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, saved_pos.y));
//         ImGui::Text("Single\n shot");
//     ImGui::EndGroup();
//     // height relative to init_pos;
//     p0 = ImGui::GetItemRectMin();
//     p1 = ImGui::GetItemRectMax() + style.FramePadding;
//     draw_list = ImGui::GetWindowDrawList();
//     draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
// 
//     ImVec2 value_text_size = ImGui::CalcTextSize("-20.0 V", NULL);
//     ImVec2 value_text_button_size = value_text_size + style.FramePadding + style.FramePadding;
//     ImVec2 value_text_button_pos = p1 + ImVec2(-value_text_button_size.x, 0.f);
//     ImGui::SetCursorScreenPos(value_text_button_pos);
//     // get_height() line 4 end
//     ButtonForSlider("##trigger_button", "##trigger_slider", value_text_button_size);
//     ImVec2 saved_pos2 = ImGui::GetCursorScreenPos();
//     float slider_bottom = saved_pos2.y - style.ItemSpacing.y;
// 
//     // get_height() line 5 end
// 
//     ImGui::SetCursorScreenPos(ImVec2(slider_left, slider_top_right.y));
// 
//     ImGui::custom_VSliderFloat("##trigger_slider", "V",
//             ImVec2(slider_top_right.x - slider_left , slider_bottom - slider_top_right.y),
//             &curr_ch_trigger_settings->trigger_level, -20.f, 20.f, "%.1f V", ImGuiSliderFlags_ClampOnInput, value_text_button_pos + style.FramePadding, value_text_size);
//     ImGui::EndDisabled();
//     ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, slider_bottom});
//     ImGui::Dummy({0.f,0.f}); // prevents issue with this draw() command affecting the vertical alignment of whatever ui element comes after it
//     ImGui::EndGroup();
//     p0 = ImGui::GetItemRectMin();
//     p1 = ImGui::GetItemRectMax();
//     draw_list = ImGui::GetWindowDrawList();
//     draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
// 
//     if(curr_ch_trigger_settings->trigger_type != o1buffer::TriggerType::Disabled)
//     {
//         both_ch_trigger_settings[ch_sel%2].trigger_type = o1buffer::TriggerType::Disabled;
//         librador_set_trigger_settings(ch_sel%2+1, both_ch_trigger_settings[ch_sel%2]);
//     }
// 
//     librador_set_trigger_settings(ch_sel, both_ch_trigger_settings[ch_sel-1]);
//     ImGui::EndGroup();
// }

// for the singlet-width version of this widget
// int triggerUI::get_height() 
// {
//     ImGuiStyle& style = ImGui::GetStyle();
//     int calc_height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
//                          CHECKBOX_SIZE + 2 * style.FramePadding.y + \
//                          2 * CHECKBOX_SIZE + 2 * style.FramePadding.y + style.ItemSpacing.y + \
//                          ImGui::CalcTextSize("Single\n shot").y + style.FramePadding.y + style.ItemSpacing.y + 2 + \
//                          ImGui::GetFontSize() + 2 * style.FramePadding.y;
//     return calc_height;
// }


#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "psu_ui.h"

void psuUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f,style.ItemSpacing.y)); // combined with lines in main.cpp, effectively folds itemspacing.y into the group covering this ui_tile.  
    ImVec2 baseline_loc = ImGui::GetCursorScreenPos();

    const float psu_button_width = style.FramePadding.x*2 + ImGui::CalcTextSize(" PSU ").x;
    ImVec2 slider_loc = baseline_loc + ImVec2(psu_button_width + style.ItemInnerSpacing.x, 0.f);
    float close_button_width = ImGui::GetFontSize() + style.FramePadding.x;
    ImVec2 close_button_loc = baseline_loc + ImVec2(width_pixels - close_button_width, style.FramePadding.y );

    ImGui::BeginGroup(); // for bounding rect
    ButtonForSlider(" PSU ", "##psu_slider", ImVec2(psu_button_width,0.f));
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(slider_loc); 
    ImGui::PushItemWidth(width_pixels - psu_button_width - 2 * style.ItemInnerSpacing.x - 1 - close_button_width);  // -1 to give space for bounding rect
    if(ImGui::custom_SliderFloat("##psu_slider", "V", &psu, 4.5f, 12.0f, "%.1f V", ImGuiSliderFlags_ClampOnInput) || ImGui::IsItemDeactivatedAfterEdit()) {
        need_usb_send = true;
    }
    SKOIA;
    ImGui::SameLine();
    ImGui::EndGroup();

    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255),0,0,2);

    if(ImGui::CloseButton(ImGui::GetID("psu_close"), close_button_loc)) {
        is_expanded = false;
        is_visible = false;
    }
    ImGui::SameLine();
    // mimic the spacing/cursor advancement that a standard Button(...) would generate
    ImGui::SetCursorScreenPos(close_button_loc);
    ImGui::Dummy({close_button_width,0.f});

    ImGui::EndGroup();

    if(need_usb_send) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_usb_send) > between_usb_sends_min) {
            usb_send_data();
            last_usb_send = now;
            need_usb_send = false;
        }
    }
}

void psuUI::usb_send_data()
{
    librador_set_power_supply_voltage(psu);
}

int psuUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return 2 * style.CellPadding.y + 2 * style.FramePadding.y + style.ItemSpacing.y + ImGui::GetFontSize();
}



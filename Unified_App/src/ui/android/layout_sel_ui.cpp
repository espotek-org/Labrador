#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include "imgui_internal.h"
#include "layout_sel_ui.h"
#include "app/App.h"

void layoutSelUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup();
    standard_header(width_pixels);
    const char *labels[3] = {"Desktop", "Tablet", "Compact"};
    App::LayoutMode modes[3] = {App::LayoutMode::Desktop, App::LayoutMode::Tablet, App::LayoutMode::Compact};
    for(int i = 0; i < 3; i++) {
        if(ImGui::Button(labels[i])) {
            layout_mode = modes[i];
        }
    }
    ImGui::EndGroup();
}

int layoutSelUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    int height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                 3 * (ImGui::GetFontSize() + (style.FramePadding.y)*2 + style.ItemSpacing.y);
    return height;
}

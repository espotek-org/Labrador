#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "daq_ui.h"
#include "SDL_system.h"

ImVec2 center_text(float col_width, float text_width, ImGuiStyle& style) {
    return ImGui::GetCursorScreenPos() + ImVec2((col_width - text_width)/2. - style.CellPadding.x, 0.0); // for centered text
}

ImVec2 center_checkbox_delta(float full_col_width, ImGuiStyle& style) {
    return ImVec2((full_col_width - CHECKBOX_SIZE)/2. - style.CellPadding.x, 0.0); // for centered checkbox
}

void daqUI::draw(float width_pixels, daqUI* daq_ui)
{

    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded)
    {
        ImGui::EndGroup();
        return;
    }

    ImVec2 start_pos = ImGui::GetCursorScreenPos();

    const char* ext_storage_path = SDL_GetAndroidExternalStoragePath();
    int buf_size = 128
    char buf[buf_size];
    strcpy(buf, ext_storage_path);
    ImGui::InputText("File", buf, 128);
}

int daqUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, style.CellPadding.y * 2));// if this line is active, make sure that the line that resets CellPadding at the end of this function is active as well
    style = ImGui::GetStyle();
    int height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                 ImGui::GetFontSize() + style.CellPadding.y*2 + \
                 4 * (ImGui::GetFontSize() + (style.CellPadding.y + style.FramePadding.y)*2);

    ImGui::PopStyleVar();
    return height;
}


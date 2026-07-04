#pragma once
#ifndef IMGUI_API
#define IMGUI_API
#endif

#include "imgui.h"
#include "imgui_internal.h"

#define SKOIA if(ImGui::IsItemActive()) SKO // required for avoiding inadvertent inputs to other widgets when trying to deactivate the most recent widget.  might want to use IsItemActiveAsInputText
#define SKO ImGui::SetKeyOwner(ImGuiKey_MouseLeft, ImGui::GetItemID())

namespace ImGui
{
// modified versions of declarations from imgui.h
    IMGUI_API bool          custom_SliderScalar(const char* label, const char* suffix, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = NULL, ImGuiSliderFlags flags = 0);
    IMGUI_API bool          custom_SliderFloat(const char* label, const char* suffix, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0);     // adjust format to decorate the value with a prefix or a suffix for in-slider labels or unit display.
    IMGUI_API bool          custom_VSliderScalar(const char* label, const char* suffix, const ImVec2& size, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = NULL, ImGuiSliderFlags flags = 0, const ImVec2 label_pos = ImVec2(), const ImVec2 label_size = ImVec2());
    IMGUI_API bool          custom_VSliderFloat(const char* label, const char* suffix, const ImVec2& size, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, const ImVec2 label_pos = ImVec2(), const ImVec2 label_size = ImVec2());     // adjust format to decorate the value with a prefix or a suffix for in-slider labels or unit display.
    IMGUI_API bool          custom_RadioButton(const char* label, int* v, int v_button);           // allow all disabled

    IMGUI_API bool          custom_Checkbox(const char* label, bool* v); // draw a line through if disabled
    IMGUI_API bool          custom_ButtonEx(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags flags = 0);

// modified versions of declarations from imgui_internal.h
    IMGUI_API bool          custom_TempInputText(const ImRect& bb, ImGuiID id, const char* label, const char* suffix, char* buf, int buf_size, ImGuiInputTextFlags flags, const ImVec2 label_pos = ImVec2(), const ImVec2 label_size = ImVec2());
    IMGUI_API bool          custom_TempInputScalar(const ImRect& bb, ImGuiID id, const char* label, const char* suffix, ImGuiDataType data_type, void* p_data, const char* format, const void* p_clamp_min = NULL, const void* p_clamp_max = NULL, const ImVec2 label_pos = ImVec2(), const ImVec2 label_size = ImVec2());
    IMGUI_API bool          custom_InputTextEx(const char* label, const char* suffix, const char* hint, char* buf, int buf_size, const ImVec2& size_arg, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback = NULL, void* user_data = NULL, const ImVec2 label_pos = ImVec2(), const ImVec2 label_size = ImVec2());
//     below from https://github.com/ocornut/imgui/issues/3379#issuecomment-2943903877
    IMGUI_API bool          ScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button);
    IMGUI_API bool          ScrollWhenDraggingAnywhere(const ImVec2& delta, ImGuiMouseButton mouse_button);
}

// new in widgets
bool ButtonForSlider(const char * button_label, const char * slider_label, ImVec2 size);


// also added AddPinchUpdateEvent to the ImGuiIO struct in imgui.h as well as in imgui.cpp

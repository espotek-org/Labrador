#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include "ui_tile.h"
#include "sig_gen_ui.h"
#include "inputs_ui.h"
#include "trigger_ui.h"
#include "virtual_transform_ui.h"
#include "psu_ui.h"
#include "logic_decode_ui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "custom_imgui.h"
#include "imgui.h"
#include "imgui_internal.h"

extern inputsUI inputs_ui;
extern triggerUI trigger_ui;
extern virtualTransformUI virtual_transform_ui;
extern sigGenUI sig_gen_ui;
extern psuUI psu_ui;
extern logicDecodeUI logic_decode_ui;

void do_settings_panel_layout(float* data_width, float* data_height, bool landscape, int y_size, float dpi, float pixel_6a_dpi);
void draw_settings_panel(bool landscape, bool screen_keyboard_shown);
void draw_selector_popup(bool landscape, bool orientation_changed);
void draw_collapse_button(bool landscape, ImVec2 dataWindowBottomLeft, ImVec2 dataWindowBottomRight);
#endif // SETTINGSPANEL_H

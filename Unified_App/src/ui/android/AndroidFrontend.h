#pragma once
#include "ui/Frontend.h"

// Mobile (phone) UI — a faithful port of Brent's Android tile UI (compact,
// touch-first): plot on top / tiled settings panel below, a tile-selector
// popup opened by tapping the panel background, and a collapse button at the
// data/panel boundary. See docs/PLAN.md and the
// labrador-mobile-ui-architecture memory.
//
// Brent kept his widgets as file-scope globals (settings_panel.cpp) and drove
// the layout with free functions + a global tiles[] array. Here those globals
// become MEMBERS of AndroidFrontend and the four layout free functions become
// member functions, so the frontend owns all of its state (no globals).

#include <chrono> // psu_ui.h/sig_gen_ui.h use std::chrono without including it
#include "imgui.h" // daq_ui.h uses ImU8; declare the imgui types first

#include "ui_tile.h"
#include "inputs_ui.h"
#include "trigger_ui.h"
#include "virtual_transform_ui.h"
#include "sig_gen_ui.h"
#include "psu_ui.h"
#include "logic_decode_ui.h"
#include "daq_ui.h"
#include "plot_ui.h"

class AndroidFrontend : public Frontend
{
  public:
    void startUp(App& app) override;
    void update(App& app) override;
    void shutDown(App& app) override;

  private:
    // Brent's widgets — previously the globals defined in settings_panel.cpp.
    inputsUI inputs_ui;
    triggerUI trigger_ui;
    virtualTransformUI virtual_transform_ui;
    sigGenUI sig_gen_ui;
    psuUI psu_ui;
    logicDecodeUI logic_decode_ui;
    daqUI daq_ui;
    plotUI plot_ui;

    // Order + membership match Brent's settings_panel.cpp tiles[] array.
    static const int n_tiles = 7;
    UI_tile* tiles[n_tiles] = { &inputs_ui, &trigger_ui, &virtual_transform_ui, &sig_gen_ui,
        &psu_ui, &logic_decode_ui, &daq_ui };

    bool m_style_tightened = false; // one-time WindowPadding halving (Brent)

    // Per-connect board init (Brent's main.cpp need_board_init state machine).
    bool need_board_init = true;

    // Layout state — previously the file-scope globals in settings_panel.cpp.
    float settings_height_max = 0.f;
    float tile_singlet_width_pixels = 0.f;
    float settings_width = 0.f;
    bool row_col_tiling = false;
    float col1_width = 0.f, col2_width = 0.f;
    float tile_col_heights[2] = { 0.f, 0.f };
    int n_singlet_tiles_visible = 0;
    float singlet_tile_height_when_row_col_tiling = 0.f;
    float settings_height = 0.f;
    bool maybe_clicked_background = false;
    bool collapse_settings = false;
    ImVec2 settings_window_center;

    // Ported from settings_panel.cpp (were free functions taking the global
    // tiles[]/inputs_ui). The dpi/pixel_6a_dpi args are dropped — AppBase owns
    // DPI scaling, so the landscape tile width is expressed in font units.
    void do_settings_panel_layout(float* data_width, float* data_height, bool landscape, int y_size);
    void draw_settings_panel(bool landscape, bool screen_keyboard_shown);
    void draw_selector_popup(bool landscape, bool orientation_changed);
    void draw_collapse_button(bool landscape, ImVec2 dataWindowBottomLeft, ImVec2 dataWindowBottomRight);
    int get_selector_popup_height();
    int get_selector_popup_width();
};

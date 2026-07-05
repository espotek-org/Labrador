// Mobile frontend — a faithful port of Brent's Android tile UI. The four
// layout routines below are lifted from Android_App settings_panel.cpp and the
// per-frame orchestration from its main.cpp loop; both became members of
// AndroidFrontend so nothing lives in globals. Only two deliberate changes were
// made vs Brent's originals:
//   * the landscape singlet-tile width is expressed in font units instead of
//     the Pixel-6a JNI DPI dance (AppBase owns DPI now);
//   * the window rect comes from App's content rect (already excludes the
//     Android status/navigation bars) instead of main.cpp's bar-height math.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "custom_imgui.h"
#include "librador.h"

#include "app/App.h"
#include "ui/android/AndroidFrontend.h"

#include <cmath>  // fmax / fmin
#include <cstring> // strcpy

// remove gaps between ui_tile groups so taps on the background don't land
// between tiles (this ItemSpacing is re-added inside each tile's group). See
// Brent's settings_panel.cpp; ImGuiContext.DebugShowGroupRects helps here.
#define INDENTUP ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(0.f, style.ItemSpacing.y));

// singlet-width tiles are tile_singlet_width_pixels wide; duplex-width tiles are
// 2 * that. two-col tiling: singlets in the left column, duplexes in the right.
// row-col tiling: singlets in a top row, duplexes stacked in a column below.
void AndroidFrontend::do_settings_panel_layout(
    float* data_width, float* data_height, bool landscape, int y_size)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    // in landscape the settings panel scrolls in y, so its width can be fixed;
    // in portrait it is full-width and the plot takes the remaining height.
    if (landscape) {
        settings_height_max = y_size;
        *data_height = y_size;
        // Brent matched the singlet width to ~1/3 of the Pixel 6a's screen in
        // physical inches (1080 px / 428.6 dpi at his 2.625x UI scale ≈ 360
        // device px ≈ 7.2 font heights). Expressed in font units it keeps the
        // same physical size on any display without the JNI DPI query.
        tile_singlet_width_pixels = 7.2f * ImGui::GetFontSize();
    } else {
        // WindowPadding.x is used between each tile and to the left/right of the group
        tile_singlet_width_pixels
            = (io.DisplaySize.x - 2 * style.WindowPadding.x - 2 * style.WindowPadding.x) / 3.;
    }
    // col1/grp1 hold singlet-width tiles, col2/grp2 hold duplex-width tiles
    tile_col_heights[0] = 0.f;
    tile_col_heights[1] = 0.f;

    n_singlet_tiles_visible = 0;
    singlet_tile_height_when_row_col_tiling = 0.f;
    for (int i = 0; i < n_tiles; i++) {
        if (tiles[i]->is_visible) {
            float height
                = tiles[i]->next_is_expanded ? tiles[i]->get_height() : tiles[i]->get_collapsed_height();
            tile_col_heights[tiles[i]->width == UI_tile::Width::singlet ? 0 : 1] += height;
            if (tiles[i]->width == UI_tile::Width::singlet) {
                singlet_tile_height_when_row_col_tiling
                    = fmax(singlet_tile_height_when_row_col_tiling, height);
                n_singlet_tiles_visible++;
            }
        }
    }

    col1_width = (n_singlet_tiles_visible > 0) ? tile_singlet_width_pixels : 0;
    col2_width = (tile_col_heights[1] > 0) ? 2 * tile_singlet_width_pixels + style.WindowPadding.x : 0;

    float row_col_tiling_height = (tile_col_heights[1] + singlet_tile_height_when_row_col_tiling);
    float two_col_tiling_height = ImMax(tile_col_heights[0], tile_col_heights[1]);

    row_col_tiling = (row_col_tiling_height < two_col_tiling_height)
        && (landscape ? (two_col_tiling_height > settings_height_max) : true);
    if (row_col_tiling) {
        settings_height = singlet_tile_height_when_row_col_tiling + tile_col_heights[1];
    } else {
        settings_height = fmax(tile_col_heights[0], tile_col_heights[1]);
    }

    if (landscape) {
        if (collapse_settings) {
            *data_width = ImGui::GetContentRegionAvail().x;
        } else {
            if (row_col_tiling) {
                settings_width = ImMax(col2_width,
                    n_singlet_tiles_visible * (tile_singlet_width_pixels + style.WindowPadding.x)
                        - style.WindowPadding.x);
            } else {
                settings_width
                    = col1_width + col2_width + ((col1_width > 0) && (col2_width > 0)) * style.WindowPadding.x;
            }
            if (settings_height > settings_height_max) {
                settings_width += style.ScrollbarSize;
            }
            settings_width = ImMax(settings_width, ImGui::GetFontSize() + 2 * style.FramePadding.x);
            *data_width = ImGui::GetContentRegionAvail().x - style.ItemSpacing.x - settings_width;
        }
    } else {
        settings_width = io.DisplaySize.x - 2 * style.WindowPadding.x;
        *data_width = settings_width;
        if (collapse_settings) {
            *data_height = ImGui::GetContentRegionAvail().y;
        } else {
            settings_height = fmax(settings_height, ImGui::GetFontSize() + 2 * style.ItemSpacing.y);
            *data_height = ImGui::GetContentRegionAvail().y - settings_height;
        }
    }
}

void AndroidFrontend::draw_settings_panel(App& app, bool landscape, bool screen_keyboard_shown)
{
    (void)app; // used on desktop builds only (layout escape hatch below)
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGuiStyle& style = ImGui::GetStyle();
    if (!landscape) {
        INDENTUP
    }
    if (!collapse_settings) {
        ImGui::BeginChild(
            "settings", ImVec2(0.f, 0.f), 0, (screen_keyboard_shown ? ImGuiWindowFlags_NoMouseInputs : 0));
        settings_window_center = ImGui::GetWindowPos() + ImGui::GetWindowSize() / 2.;
        ImVec2 settings_start_pos = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("open ui_tile selector",
                ImGui::GetContentRegionAvail()
                    + ImVec2(0.f, (settings_height > settings_height_max) ? ImGui::GetScrollY() : 0.f))) {
            maybe_clicked_background = true;
        }
        ImGui::SetCursorScreenPos(settings_start_pos);

        if (row_col_tiling) {
            for (int grp : { 0, 1 }) {
                if (grp == 1)
                    INDENTUP
                ImGui::BeginGroup();
                bool first = true;
                UI_tile::Width grp_width_type = (UI_tile::Width)grp; // 0: singlet; 1: duplex

                for (int i = 0; i < n_tiles; i++) {
                    if (tiles[i]->is_visible && (tiles[i]->width == grp_width_type)) {
                        if ((grp == 1) && (!first)) {
                            INDENTUP
                        }
                        first = false;
                        tiles[i]->draw(grp_width_type == UI_tile::Width::singlet
                                ? tile_singlet_width_pixels
                                : (2 * tile_singlet_width_pixels + style.WindowPadding.x),
                            &inputs_ui);
                        maybe_clicked_background &= !ImGui::IsItemHovered();
                        // group 0 items stack side-by-side; group 1 items stack vertically
                        if (grp == 0) {
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(style.WindowPadding.x, style.ItemSpacing.y));
                            ImGui::SameLine(); // invalidated after EndGroup, convenient here
                            ImGui::PopStyleVar();
                        }
                    }
                }
                ImGui::EndGroup();
            }
        } else {
            for (int col : { 0, 1 }) {
                UI_tile::Width col_width_type = (UI_tile::Width)col; // 0: singlet; 1: duplex

                if (tile_col_heights[col] > 0) {
                    ImGui::BeginGroup();
                    bool first = true;
                    for (int i = 0; i < n_tiles; i++) {
                        if (tiles[i]->is_visible && (tiles[i]->width == col_width_type)) {
                            if (!first)
                                INDENTUP
                            first = false;
                            tiles[i]->draw(col_width_type == UI_tile::Width::singlet
                                    ? tile_singlet_width_pixels
                                    : (2 * tile_singlet_width_pixels + style.WindowPadding.x),
                                &inputs_ui);
                            maybe_clicked_background &= !ImGui::IsItemHovered();
                        }
                    }
                    ImGui::EndGroup();
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_ItemSpacing, ImVec2(style.WindowPadding.x, style.ItemSpacing.y));
                    ImGui::SameLine();
                    ImGui::PopStyleVar();
                }
            }
            ImGui::NewLine();
        }

#ifndef __ANDROID__
        // Desktop testing escape hatch: the tile UI has no View menu, so the
        // way back to the desktop layout lives under the control tiles,
        // sized like a tile so it reads as part of the column.
        ImGui::Spacing();
        if (ImGui::Button("Desktop layout", ImVec2(tile_singlet_width_pixels, 0.f)))
            app.setLayoutMode(App::LayoutMode::Desktop);
        maybe_clicked_background &= !ImGui::IsItemHovered();
#endif

        ImGui::EndChild();
    }
}

int AndroidFrontend::get_selector_popup_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return style.WindowPadding.y + style.WindowBorderSize + style.ItemSpacing.y + ImGui::GetFontSize()
        + style.ItemSpacing.y + style.SeparatorSize + CHECKBOX_SIZE * n_tiles
        + style.ItemSpacing.y * (n_tiles - 1) + style.WindowPadding.y + style.WindowBorderSize;
}

int AndroidFrontend::get_selector_popup_width()
{
    ImGuiStyle& style = ImGui::GetStyle();
    float text_width = 0;
    for (int i = 0; i < n_tiles; i++) {
        text_width = fmax(text_width, ImGui::CalcTextSize(tiles[i]->name).x);
    }
    return CHECKBOX_SIZE + style.ItemInnerSpacing.x + text_width
        + 2 * (style.WindowBorderSize + style.WindowPadding.x);
}

void AndroidFrontend::draw_selector_popup(bool landscape, bool orientation_changed)
{
    if (maybe_clicked_background || (orientation_changed && ImGui::IsPopupOpen("config_settings"))) {
        ImVec2 main_window_bottom_right = ImGui::GetWindowPos() + ImGui::GetWindowSize();
        ImVec2 centered_selector_window_bottom_right
            = settings_window_center + ImVec2(get_selector_popup_width(), get_selector_popup_height()) / 2.;
        if ((landscape && (centered_selector_window_bottom_right.x > main_window_bottom_right.x))
            || (!landscape && (centered_selector_window_bottom_right.y > main_window_bottom_right.y))) {
            ImVec2 edge_selector_window_pos = ImGui::GetWindowPos()
                + ImVec2((ImGui::GetWindowSize().x - get_selector_popup_width()) * (landscape ? 1. : 0.5),
                    (ImGui::GetWindowSize().y - get_selector_popup_height()) * (landscape ? 0.5 : 1));
            ImGui::SetNextWindowPos(edge_selector_window_pos, 0, ImVec2(0.f, 0.f));
        } else {
            ImGui::SetNextWindowPos(settings_window_center, 0, ImVec2(0.5f, 0.5f));
        }
        ImGui::OpenPopup("config_settings");
        maybe_clicked_background = false;
    }
    if (ImGui::BeginPopup("config_settings")) {
        ImGui::Text("Select tiles");
        ImGui::Separator();
        for (int i = 0; i < n_tiles; i++) {
            if (ImGui::Checkbox(tiles[i]->name, &tiles[i]->is_visible)) {
                if (tiles[i]->is_visible) {
                    tiles[i]->next_is_expanded = true;
                }
            }
        }
        ImGui::EndPopup();
    }
}

void AndroidFrontend::draw_collapse_button(
    bool landscape, ImVec2 dataWindowBottomLeft, ImVec2 dataWindowBottomRight)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 collapse_button_pos;
    char label[36];
    if (landscape) {
        if (collapse_settings) {
            strcpy(label, " < ");
        } else {
            strcpy(label, " > ");
        }
        collapse_button_pos = dataWindowBottomRight - ImGui::CalcTextSize(" < ") - style.FramePadding * 2;
    } else {
        if (collapse_settings) {
            strcpy(label, " ^ ");
        } else {
            strcpy(label, " v ");
        }
        collapse_button_pos
            = dataWindowBottomLeft - ImVec2(0.f, ImGui::CalcTextSize(" ^ ").y + style.FramePadding.y * 2);
    }
    ImGui::BeginChild("data"); // to append to the 'plot' child, re-enter each parent
    ImGui::BeginChild("plot");
    ImGui::SetCursorScreenPos(collapse_button_pos);
    if (ImGui::custom_ButtonEx(label)) {
        collapse_settings = !collapse_settings;
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void AndroidFrontend::startUp(App& app)
{
    (void)app;
    // Tile visibility/expansion defaults, verbatim from Brent's main.cpp setup:
    // only Inputs, Logic Decoding and DAQ start visible; the rest are revealed
    // via the tile selector.
    virtual_transform_ui.is_visible = true;
    virtual_transform_ui.is_expanded = false;
    virtual_transform_ui.next_is_expanded = false;
    psu_ui.is_expanded = false;
    psu_ui.next_is_expanded = false;
    psu_ui.is_visible = false;
    trigger_ui.next_is_expanded = false;
    trigger_ui.is_visible = false;
    virtual_transform_ui.next_is_expanded = false;
    virtual_transform_ui.is_visible = false;
    sig_gen_ui.next_is_expanded = false;
    sig_gen_ui.is_visible = false;
}

void AndroidFrontend::update(App& app)
{
    ImGuiStyle& style = ImGui::GetStyle();

    // Brent tightened WindowPadding once at startup (main.cpp). SetGlobalStyle
    // resets only colours each frame, so a one-shot halving is safe (repeating
    // it every frame would shrink padding toward zero).
    if (!m_style_tightened) {
        style.WindowPadding = ImVec2(style.WindowPadding.x / 2, style.WindowPadding.y / 2);
        m_style_tightened = true;
    }

    // --- Per-connect board init (Brent's main.cpp need_board_init flow). The
    // iso-thread check must run each frame: on an iso-thread-active transition
    // we (re)push device mode, both signal-gen channels, PSU and scope gain. ---
    bool iso_thread_active = librador_iso_thread_is_active();
    if (!iso_thread_active)
        need_board_init = true;
    if (need_board_init && iso_thread_active) {
        inputs_ui.update_device_mode();
        sig_gen_ui.usb_send_data(1);
        sig_gen_ui.usb_send_data(2);
        psu_ui.usb_send_data();
        librador_set_oscilloscope_gain(4.);
        need_board_init = false;
    }

    // --- Orientation (from App's content rect, which excludes system bars) ---
    const float content_w = app.contentW();
    const float content_h = app.contentH();
    static bool landscape = content_h < content_w;
    bool new_landscape = content_h < content_w;
    bool orientation_changed = (landscape != new_landscape);
    landscape = new_landscape;

    // --- Per-frame widget servicing (Brent's main loop) ---
    plot_ui.recompute_x_bounds(inputs_ui.changed_since_last(), inputs_ui.mode);
    logic_decode_ui.update(&inputs_ui);
    daq_ui.poll_status();

    ImGui::SetNextWindowPos(ImVec2(app.contentX(), app.contentY()));
    ImGui::SetNextWindowSize(ImVec2(content_w, content_h));

    bool show_mainwindow = true;
    bool screen_keyboard_shown = false;
    ImGui::Begin("MainWindow", &show_mainwindow,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoMove);

    float data_width;
    float data_height;
    do_settings_panel_layout(
        &data_width, &data_height, landscape, content_h - 2 * style.WindowPadding.y);

    ImGui::BeginChild("data", ImVec2(data_width, data_height), 0,
        (screen_keyboard_shown ? ImGuiWindowFlags_NoMouseInputs : 0));
    {
        if (logic_decode_ui.decoding_on()) {
            logic_decode_ui.draw_console(data_width);
        }
        plot_ui.draw(iso_thread_active, inputs_ui.mode, inputs_ui.ch_enabled(1),
            inputs_ui.ch_enabled(2), data_width, 0.);
    }
    ImVec2 dataWindowBottomLeft = ImGui::GetWindowPos() + ImVec2(0.f, ImGui::GetWindowSize().y);
    ImVec2 dataWindowBottomRight = ImGui::GetWindowPos() + ImGui::GetWindowSize();
    ImGui::EndChild();
    if (landscape) {
        ImGui::SameLine();
    }

    draw_selector_popup(landscape, orientation_changed);
    draw_settings_panel(app, landscape, screen_keyboard_shown);
    draw_collapse_button(landscape, dataWindowBottomLeft, dataWindowBottomRight);

    ImGui::End();
}

void AndroidFrontend::shutDown(App& app)
{
    (void)app;
}

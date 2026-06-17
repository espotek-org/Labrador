#include "settings_panel.h"

inputsUI inputs_ui = inputsUI();
triggerUI trigger_ui = triggerUI();
virtualTransformUI virtual_transform_ui = virtualTransformUI();
sigGenUI sig_gen_ui = sigGenUI();
psuUI psu_ui = psuUI();
logicDecodeUI logic_decode_ui = logicDecodeUI();
daqUI daq_ui = daqUI();

const int n_tiles = 7;
UI_tile* tiles[n_tiles] = {&inputs_ui, &trigger_ui, &virtual_transform_ui, &sig_gen_ui, &psu_ui, &logic_decode_ui, &daq_ui}; 

float pixel_6a_screen_width = 1080.f;
float pixel_6a_setting_panel_aspect = 1.13; // width to height
float settings_height_max;
float adjustment;
float tile_singlet_width_pixels;
float settings_width;
bool row_col_tiling;
float col1_width, col2_width;
float tile_col_heights[2];
int n_singlet_tiles_visible;
float singlet_tile_height_when_row_col_tiling;
float settings_height;

bool maybe_clicked_background = false;
bool collapse_settings = false;

ImVec2 settings_window_center;

// singlet-width tiles are (tile_singlet_width_pixels + adjustment) wide
// duplex-width tiles are (2 * tile_singlet_width_pixels - adjustment) wide
// two-col tiling: two columns of tiles side-by-side, the one on the left containing singlet-width tiles and the one on the right containing duplex-width tiles
// row-col tiling: draw singlet-width tiles in the top row; below this row, draw duplex-width tiles in a column
void do_settings_panel_layout(float* data_width, float* data_height, bool landscape, int y_size, float dpi, float pixel_6a_dpi) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    adjustment = 0.f;
    // should compute these values only once, but there's no way to get the navigation/status bar heights in landscape mode when in portrait mode or vice-versa; it's necessary to wait until the device actually enters a given orientation to access the heights
    // in landscape mode, allow scrolling the settings panel in the y direction, so there's no need to account for different y sizes across devices
    if(landscape) { 
        settings_height_max = y_size;
        *data_height = y_size;
        tile_singlet_width_pixels = settings_height_max * pixel_6a_setting_panel_aspect / 3.f;
    } else {
        // if the current device's screen is smaller width-wise than the pixel 6a's screen, make sure the singlet-width tiles remain the same width in inches as on the pixel 6a.  do this by transfering space from the duplex-width tiles (which aren't as space-constrained)
        adjustment = ((pixel_6a_screen_width * dpi / pixel_6a_dpi) - static_cast<double>(io.DisplaySize.x))/3.; 
        adjustment = adjustment < 0 ? 0 : adjustment;
        // WindowPadding.x is used between (left/right-wise) each tile and also to the left and right of the tile group
        tile_singlet_width_pixels = (io.DisplaySize.x - 2 * style.WindowPadding.x - 2 * style.WindowPadding.x)/3.;
    }
    // col1 and grp1 contain singlet-width tiles, col2 and grp2 have duplex-width tiles
    tile_col_heights[0] = 0.f;
    tile_col_heights[1] = 0.f;

    n_singlet_tiles_visible = 0;
    singlet_tile_height_when_row_col_tiling = 0.f; 
    for(int i=0; i < n_tiles; i++) {
        if(tiles[i]->is_visible) {
            float height = tiles[i]->next_is_expanded ? tiles[i]->get_height() : tiles[i]->get_collapsed_height();
            tile_col_heights[static_cast<int>(tiles[i]->width)] += height;
            if(tiles[i]->width == UI_tile::Width::singlet) {
                singlet_tile_height_when_row_col_tiling = fmax(singlet_tile_height_when_row_col_tiling, height);
                n_singlet_tiles_visible++;
            }
        }
    }

    // these widths only relevent to two-col tiling
    col1_width = (n_singlet_tiles_visible > 0) ? tile_singlet_width_pixels : 0;
    col2_width = (tile_col_heights[1] > 0) ? 2 * tile_singlet_width_pixels : 0;

    row_col_tiling = (!landscape && (n_singlet_tiles_visible >= 2) && ((tile_col_heights[1] + singlet_tile_height_when_row_col_tiling) < fmax(tile_col_heights[0], tile_col_heights[1]))) || \
        (landscape && (n_singlet_tiles_visible > 0) && (tile_col_heights[1] > 0) && ((tile_col_heights[1] + singlet_tile_height_when_row_col_tiling) < settings_height_max));

    if(landscape) {
        if(collapse_settings) {
            *data_width = ImGui::GetContentRegionAvail().x;
        } else {
            if(row_col_tiling) {
                if (tile_col_heights[1] > 0.f) {
                    settings_width = 2 * tile_singlet_width_pixels + style.ItemSpacing.x;
                } else if (n_singlet_tiles_visible > 0) {
                    settings_width = tile_singlet_width_pixels + (n_singlet_tiles_visible == 2) * (tile_singlet_width_pixels + style.ItemSpacing.x);
                } else {
                    settings_width = 0.f;
                }
            } else {
                settings_width = col1_width + col2_width + ((col1_width>0)&&(col2_width>0)) * style.ItemSpacing.x;
                float max_col_height = fmax(tile_col_heights[0], tile_col_heights[1]);
                if(max_col_height > settings_height_max) {
                    settings_width += style.ScrollbarSize;
                }
            }
            settings_width = fmax(settings_width, ImGui::GetFontSize() + 2 * style.FramePadding.x);
            *data_width = ImGui::GetContentRegionAvail().x - style.ItemSpacing.x - settings_width;
        }
    } else {
        settings_width = io.DisplaySize.x - 2 * style.WindowPadding.x;
        *data_width = settings_width;
        if(collapse_settings) {
            *data_height = ImGui::GetContentRegionAvail().y;
        } else {
            if(row_col_tiling) {
                settings_height = singlet_tile_height_when_row_col_tiling + tile_col_heights[1];
            } else {
                settings_height = fmax(tile_col_heights[0], tile_col_heights[1]);
            }
            settings_height = fmax(settings_height, ImGui::GetFontSize() + 2 * style.ItemSpacing.y);
            *data_height = ImGui::GetContentRegionAvail().y - settings_height;
        }
    }
}

#define INDENTUP ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(0.f,style.ItemSpacing.y)); // remove gaps between ui_tile groups in order to avoid unwanted presses on the background that open the ui_tile selector popup.  this ItemSpacing is added back in by the tiles within their BeginGroup()/EndGroup() wrappings.  ImGuiContext.DebugShowGroupRects is very handy for debugging the groups

void draw_settings_panel(bool landscape, bool screen_keyboard_shown) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    if(!landscape) {
        INDENTUP
    }
    if(!collapse_settings) {
        ImGui::BeginChild("settings",ImVec2(0.f, 0.f), 0, (screen_keyboard_shown ? ImGuiWindowFlags_NoMouseInputs : 0));
        settings_window_center = ImGui::GetWindowPos() + ImGui::GetWindowSize()/2.;
        ImGuiContext& g = *GImGui;
        ImVec2 settings_start_pos = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        if(ImGui::InvisibleButton("open ui_tile selector", {0.f, 0.f})) {
                maybe_clicked_background = true;
        }
        ImGui::SetCursorScreenPos(settings_start_pos);

        if(row_col_tiling) {
            for(int grp : {0,1}) {
                if(grp==1)
                    INDENTUP
                ImGui::BeginGroup();
                bool first = true;
                UI_tile::Width grp_width = (UI_tile::Width) grp; //0: singlet; 1: duplex

                for(int i=0; i<n_tiles; i++) {
                    if (tiles[i]->is_visible && (tiles[i]->width == grp_width)) {
                        if((grp==1)&&(!first)) {
                            INDENTUP
                        }
                        first = false;
                        tiles[i]->draw(tile_singlet_width_pixels + grp * (tile_singlet_width_pixels + style.ItemSpacing.x), &inputs_ui);
                        maybe_clicked_background &= !ImGui::IsItemHovered();
                        // items in group 0 are stacked side-by-side; those in group 1 are stacked vertically
                        if(grp==0) {
                            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.WindowPadding.x, style.ItemSpacing.y));
                            ImGui::SameLine(); // seems to be invalidated after endgroup, which is convenient here
                            ImGui::PopStyleVar();
                        }
                    }
                }
                ImGui::EndGroup();
            }
        } else {
            for(int col : {0,1}) {
                UI_tile::Width col_width_type = (UI_tile::Width) col; // 0: singlet; 1: duplex
                int col_width;
                if(col_width_type == UI_tile::Width::singlet) {
                    col_width = tile_singlet_width_pixels;
                } else {
                    col_width = 2 * tile_singlet_width_pixels + style.WindowPadding.x;
                }

                if(tile_col_heights[col] > 0)
                {
                    ImGui::BeginGroup();
                    bool first = true;
                    for(int i=0; i<n_tiles; i++) {
                        if (tiles[i]->is_visible && (tiles[i]->width == col_width_type)) {
                            if(!first)
                                INDENTUP
                            first=false;
                            if(landscape) {
                                tiles[i]->draw(col_width, &inputs_ui);
                            } else {
                                // widths:
                                // duplex-width tiles: 2 * tile_singlet_width_pixels - adjustment 
                                // singlet-width tiles: tile_singlet_width_pixels + adjustment
                                tiles[i]->draw(col_width, &inputs_ui);
                            }
                            maybe_clicked_background &= !ImGui::IsItemHovered();
                        }
                    }
                    ImGui::EndGroup();
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.WindowPadding.x, style.ItemSpacing.y));
                    ImGui::SameLine();
                    ImGui::PopStyleVar();
                }
            }
            ImGui::NewLine();
        }
        ImGui::EndChild();

    }
}

int get_selector_popup_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return style.WindowPadding.y + style.WindowBorderSize + \
            style.ItemSpacing.y + ImGui::GetFontSize() + \
            style.ItemSpacing.y + style.SeparatorSize + \
            CHECKBOX_SIZE * n_tiles + style.ItemSpacing.y * (n_tiles - 1) + \
            style.WindowPadding.y + style.WindowBorderSize;
}

int get_selector_popup_width()
{
    ImGuiStyle& style = ImGui::GetStyle();
    float text_width = 0;
    for(int i = 0; i < n_tiles; i++) {
        text_width = fmax(text_width, ImGui::CalcTextSize(tiles[i]->name).x);
    }
    return CHECKBOX_SIZE + style.ItemInnerSpacing.x + text_width + 2 * (style.WindowBorderSize + style.WindowPadding.x);
}

void draw_selector_popup(bool landscape, bool orientation_changed) {
    if(maybe_clicked_background || (orientation_changed && ImGui::IsPopupOpen("config_settings"))) {
        ImVec2 main_window_bottom_right = ImGui::GetWindowPos() + ImGui::GetWindowSize();
        ImVec2 centered_selector_window_bottom_right = settings_window_center + ImVec2(get_selector_popup_width(), get_selector_popup_height())/2.;
        if(\
            (landscape && (centered_selector_window_bottom_right.x > main_window_bottom_right.x)) || 
            (!landscape && (centered_selector_window_bottom_right.y > main_window_bottom_right.y))) {
            ImVec2 edge_selector_window_pos = ImGui::GetWindowPos() + \
                                     ImVec2((ImGui::GetWindowSize().x - get_selector_popup_width()) * (landscape ? 1. : 0.5), (ImGui::GetWindowSize().y - get_selector_popup_height()) * (landscape ? 0.5 : 1));
            ImGui::SetNextWindowPos(edge_selector_window_pos,0,ImVec2(0.f,0.f));
        } else {
            ImGui::SetNextWindowPos(settings_window_center,0,ImVec2(0.5f,0.5f));
        }
        ImGui::OpenPopup("config_settings");
        maybe_clicked_background = false;
    }
    if(ImGui::BeginPopup("config_settings")) {
        ImGui::Text("Select tiles");
        ImGui::Separator();
        for (int i=0; i< n_tiles; i++) {
            if(ImGui::Checkbox(tiles[i]->name, &tiles[i]->is_visible)) {
                if(tiles[i]->is_visible)
                {
                    tiles[i]->next_is_expanded = true; 
                }
            }
        }
        ImGui::EndPopup();
    }
}

void draw_collapse_button(bool landscape, ImVec2 dataWindowBottomLeft, ImVec2 dataWindowBottomRight) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiID collapse_id = ImGui::GetID("collapse");
    ImVec2 collapse_button_pos;
    char label[36];
    if(landscape) {
        if(collapse_settings) {
            strcpy(label, " < ");
        } else {
            strcpy(label, " > ");
        }
        collapse_button_pos = dataWindowBottomRight - ImGui::CalcTextSize(" < ") - style.FramePadding * 2;
    } else {
        if(collapse_settings) {
            strcpy(label, " ^ ");
        } else {
            strcpy(label, " v ");
        }
        collapse_button_pos = dataWindowBottomLeft - ImVec2(0.f,ImGui::CalcTextSize(" ^ ").y + style.FramePadding.y * 2);
    }
    ImGui::BeginChild("data"); // to append to the 'plot' child, need to re-enter into each of its parents
    ImGui::BeginChild("plot");
    ImGui::SetCursorScreenPos(collapse_button_pos);
    if(ImGui::custom_ButtonEx(label)) {
        collapse_settings = !collapse_settings;
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

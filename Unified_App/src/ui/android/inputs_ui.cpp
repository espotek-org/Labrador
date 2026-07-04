#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "inputs_ui.h"

ImVec2 center_text(float col_width, float text_width, ImGuiStyle& style) {
    return ImGui::GetCursorScreenPos() + ImVec2((col_width - text_width)/2. - style.CellPadding.x, 0.0); // for centered text
}

ImVec2 center_checkbox_delta(float full_col_width, ImGuiStyle& style) {
    return ImVec2((full_col_width - CHECKBOX_SIZE)/2. - style.CellPadding.x, 0.0); // for centered checkbox
}

void draw_rules(ImVec2 p0, double row_height, double header_row_height, double col_width)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 row1_col1_pos = p0 + ImVec2(col_width, header_row_height);
    ImVec2 row1_col2_pos = row1_col1_pos + ImVec2(col_width,0);
    ImVec2 row3_col2_pos = row1_col1_pos + ImVec2(col_width,2*row_height);
    ImVec2 row4_col1_pos = row1_col1_pos + ImVec2(0.,3*row_height);
    ImVec2 row4_col2_pos = row1_col1_pos + ImVec2(col_width,3*row_height);
    ImVec2 row5_col2_pos = row1_col1_pos + ImVec2(col_width,4*row_height);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(row1_col1_pos, row4_col1_pos, IM_COL32(120, 120, 160, 255));
    draw_list->AddLine(row1_col2_pos, row3_col2_pos, IM_COL32(120, 120, 160, 255));
    draw_list->AddLine(row4_col2_pos, row5_col2_pos, IM_COL32(120, 120, 160, 255));
}

void inputsUI::draw(float width_pixels, inputsUI* inputs_ui)
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

    bool mode_update = false;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, style.CellPadding.y * 2));// if this line is active, make sure that the line that resets CellPadding at the end of this function is active as well
    float header_row_height = ImGui::GetFontSize() + style.CellPadding.y*2;
    float row_height = (ImGui::GetFontSize() + (style.FramePadding.y + style.CellPadding.y)*2);
    float col_width;
    if (ImGui::BeginTable("scope_mode", 3, ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg , ImVec2(width_pixels,0.f)))
    {
        ImGui::TableNextRow();
        int i = 0;
        for(const char * ch_header : {"1","2","CH"})
        {
            ImGui::TableNextColumn();
            ImGui::SetCursorScreenPos(center_text(ImGui::GetColumnWidth() + 2*style.CellPadding.x, ImGui::CalcTextSize(ch_header).x, style));
            ImGui::Text("%s", ch_header);
            col_width = ImGui::GetColumnWidth() + 2 * style.CellPadding.x;
            i+=1;
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg) );
        }

        bool scope_checkbox_enable[2] = {
            !logic_enable[1] && !mm,
            scope_enable[0] && !(logic_enable[0]) && !scope750
        };
        bool logic_checkbox_enable[2] = {
            !scope_enable[1] && !scope750 && !mm,
            logic_enable[0] && !scope_enable[0]
        };

        bool checkbox_enable[2][2] = {
            {scope_checkbox_enable[0], scope_checkbox_enable[1]},
            {logic_checkbox_enable[0], logic_checkbox_enable[1]}
        };

        bool* checkbox_bool[2][2] = {
            {&scope_enable[0], &scope_enable[1]},
            {&logic_enable[0], &logic_enable[1]}
        };
        const char * checkbox_label_base[2] = {"##enable_scope", "##enable_logic"};
        const char * glyphs[2] = {"\xee\xa4\x81","\xee\xa4\x80"};// includes custom glyphs defined in /font/waveform-glyphs3.ttf
        const char * checkbox_label_suffix[2] = {"_ch1","_ch2"};
        char full_checkbox_label[32]{};
        for(int j=0; j<2; j++)
        {
            ImGui::TableNextRow();
            for(int ch : {1,2} )
            {
                ImGui::TableNextColumn();
                strcpy(full_checkbox_label, checkbox_label_base[j]);
                strcat(full_checkbox_label, checkbox_label_suffix[ch-1]);
                ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + center_checkbox_delta(ImGui::GetColumnWidth() + 2*style.CellPadding.x, style));
                ImGui::BeginDisabled(!(checkbox_enable[j][ch-1]));
                if((ImGui::custom_Checkbox(full_checkbox_label, checkbox_bool[j][ch-1]))||(*checkbox_bool[j][ch-1] && !checkbox_enable[j][ch-1])) {
                    mode_update = true;
                    changed = true;
                }
                *(checkbox_bool[j][ch-1]) &= checkbox_enable[j][ch-1];

                ImGui::EndDisabled();
            }

            ImGui::TableNextColumn();
            ImGui::SetCursorScreenPos(center_text(ImGui::GetColumnWidth() + 2*style.CellPadding.x, ImGui::CalcTextSize(glyphs[j]).x, style));
            ImGui::Text("%s",glyphs[j]); 
        }
        ImGui::TableNextRow(0, row_height);
        ImGui::TableNextRow(0, row_height);
        ImGui::EndTable();
    }
    ImVec2 saved_pos = ImGui::GetCursorScreenPos();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 end_pos = ImGui::GetItemRectMax();
    int real_height = (end_pos - start_pos).y;
    draw_rules(p0, row_height, header_row_height, col_width);
    ImVec2 row3_pos = p0 + ImVec2(style.CellPadding.x,header_row_height + 2*row_height + style.CellPadding.y);;
    ImVec2 row4_pos = row3_pos + ImVec2(0.f,row_height);

    bool* checkbox_bool[2] = {&scope750, &mm};
    bool checkbox_enable[2] = {scope_enable[0] && !(scope_enable[1] || logic_enable[0]), (!scope_enable[0] && !scope_enable[1] && !logic_enable[0] && !logic_enable[1])};
    ImVec2 positions[2] = 
    {
        row3_pos + center_checkbox_delta(col_width, style),
        row4_pos + ImVec2(2 * col_width  ,0.f) + center_checkbox_delta(col_width, style)
    };
    const char* internal_labels[2] = {"##750 kHz","##multimeter Mode"};

    for(int i = 0; i < 2; i++)
    {
        ImGui::SetCursorScreenPos(positions[i]);
        ImGui::BeginDisabled(!checkbox_enable[i]);
        if((ImGui::custom_Checkbox(internal_labels[i], checkbox_bool[i])) || (*checkbox_bool[i] && !checkbox_enable[i])) {
            changed = true;
            mode_update = true;
        }
        *checkbox_bool[i] &= checkbox_enable[i];
        ImGui::EndDisabled();
    }
    ImGui::SetCursorScreenPos(row3_pos + ImVec2(col_width - style.CellPadding.x + (2*col_width - ImGui::CalcTextSize("750 kHz").x)/2.,2 * style.FramePadding.y - style.ItemSpacing.y));
    ImGui::Text("750 kHz");
    ImGui::PushFont(NULL, style.FontSizeBase * 1.3);
    ImGui::SetCursorScreenPos(row4_pos + ImVec2(col_width - style.CellPadding.x - ImGui::CalcTextSize("\xee\xa4\x82").x/2.,0.f));
    ImGui::Text("\xee\xa4\x82");
    ImGui::PopFont();
    ImGui::PopStyleVar();
//     ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.f,0.f});
//     ImGui::Dummy({0.f, saved_pos.y - ImGui::GetCursorScreenPos().y});
//     ImGui::PopStyleVar();
    ImGui::EndGroup();

    if (mode_update)
        update_device_mode();
}

bool inputsUI::logic_AB_enabled(int ch)
{
    // for argument 'ch': 1==ChA ; 2==ChB, where ChA/B refer to plotted lines.  Except in ScopeLogic and Multimeter modes, ChA is scope or logic Ch1 and ChB is scope or logic Ch2.
    if(mode == Mode::ScopeLogic)
        return ch==2;
    else if (mode == Mode::Multimeter)
        return false;
    else
        return logic_enable[ch-1];
}

bool inputsUI::ch_enabled(int ch)
{
    // for argument 'ch': 1==ChA ; 2==ChB, where ChA/B refer to plotted lines.  Except in ScopeLogic and Multimeter modes, ChA is scope or logic Ch1 and ChB is scope or logic Ch2.
    if(mode == Mode::ScopeLogic)
        return true;
    else if (mode == Mode::Multimeter)
        return ch==1;
    else
        return scope_enable[ch-1]||logic_enable[ch-1];
    return false;
}

bool inputsUI::changed_since_last()
{
    bool changed_temp = changed;
    changed = false;
    return changed_temp;
}

bool inputsUI::logic_on()
{
    return (logic_enable[0] || logic_enable[1]);
}

bool inputsUI::scopelogic_mode()
{
    return mode == Mode::ScopeLogic;
}

int inputsUI::get_height()
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

void inputsUI::update_device_mode()
{
    if(scope_enable[0] && !scope_enable[1] && !logic_enable[0] && !scope750)
        mode = Mode::Ch1Scope;
    else if (scope_enable[0] && logic_enable[0])
        mode = Mode::ScopeLogic;
    else if (scope_enable[0] && scope_enable[1])
        mode = Mode::ScopeScope;
    else if (logic_enable[0] && !logic_enable[1] && !scope_enable[0])
        mode = Mode::Ch1Logic;
    else if (logic_enable[0] && logic_enable[1])
        mode = Mode::LogicLogic;
    else if (!(scope_enable[0] || scope_enable[1] || logic_enable[0] || logic_enable[1] || mm))
        mode = Mode::None;
    else if (scope750)
        mode = Mode::Scope750;
    else if (mm)
        mode = Mode::Multimeter;

    librador_set_device_mode((int) mode);
}


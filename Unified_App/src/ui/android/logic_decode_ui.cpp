#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "librador.h"
#include "custom_imgui.h"
#include "imgui_internal.h"
#include "logic_decode_ui.h"
#include "inputs_ui.h"

float logicDecodeUI::draw_grabber(float grabber_height, const char * label, float* backlog, int ch, bool parity_check)
{
    ImGui::PushID(ch);
    char chAB[2] = {'A', 'B'};
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 init_pos = ImGui::GetCursorScreenPos();
    ImVec2 end_pos = init_pos + ImVec2(ImGui::GetContentRegionAvail().x,0.f);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(label, ImVec2(-1, grabber_height - 2 * style.ItemSpacing.y));
    if (ImGui::IsItemActivated()) {
        *backlog = 0.f;
    }
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    float hcenter = (p0.x + p1.x)/2.;
    float ycenter = (p0.y + p1.y)/2.;
    float yspan = (p1.y - p0.y)/2.;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(ImVec2(hcenter - ImGui::GetFontSize(), ycenter - yspan/2),ImVec2(hcenter + ImGui::GetFontSize(), ycenter - yspan/2), IM_COL32(120, 120, 160, 255));
    draw_list->AddLine(ImVec2(hcenter - ImGui::GetFontSize(), ycenter),ImVec2(hcenter + ImGui::GetFontSize(), ycenter ), IM_COL32(120, 120, 160, 255));
    draw_list->AddLine(ImVec2(hcenter - ImGui::GetFontSize(), ycenter + yspan/2),ImVec2(hcenter + ImGui::GetFontSize(), ycenter + yspan/2), IM_COL32(120, 120, 160, 255));
    float return_val = 0.f;
    if (ImGui::IsItemActive()) {
        float mouse_delta = ImGui::GetIO().MouseDelta.y;
        if( (*backlog==0) || ((mouse_delta > 0) == (*backlog > 0)) ) {
            return_val = mouse_delta;
        } else {
            *backlog += mouse_delta;
        }
    }

    // uart settings
    uart_settings* curr_ch_uart_settings = &both_ch_uart_settings[ch-1];
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetColorU32(ImGuiCol_ChildBg,0));
    bool uart_changed = false;
    ImGui::SetCursorScreenPos(init_pos);
    ImVec2 positions[2] = {init_pos, end_pos + ImVec2(-ImGui::CalcTextSize("Even").x - 2 * style.FramePadding.x, 0.f)};
    const char ** uart_options_sublabels[2] = {baud_rate_labels, parity_labels};
    int sublabels_counts[2] = {IM_COUNTOF(baud_rate_labels), IM_COUNTOF(parity_labels)};
    int * curr_options_sel[2] = {&curr_ch_uart_settings->baud_idx_sel, &curr_ch_uart_settings->parity_idx_sel};
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.0);
    for(int k: {0,1})
    {
        ImGui::PushID(k);
        ImGui::SetCursorScreenPos(positions[k]);
        if(k==0) {
            ImGui::PushItemWidth(ImGui::CalcTextSize(" A ").x + 2*style.FramePadding.x);
            ImGui::LabelText("##console_ch_label"," %c ",chAB[ch-1]);
            p0 = ImGui::GetItemRectMin() + style.FramePadding;
            p1 = ImGui::GetItemRectMax() - style.FramePadding;
            draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 255));
            ImGui::SameLine();
        }

        ImGui::PushItemWidth(ImGui::CalcTextSize(uart_options_sublabels[k][*curr_options_sel[k]]).x + 2 * style.FramePadding.x);
#define POP_COLOR if(need_pop) {ImGui::PopStyleColor(); need_pop = false;}
        
        ImU32 label_col = IM_COL32(255,255,255,255);

        bool need_pop = false;
        if(k==1 && !parity_check) {
            label_col = IM_COL32(255,0,0,255);
            ImGui::PushStyleColor(ImGuiCol_Text, label_col);
            need_pop = true;
        }
        if(ImGui::BeginCombo("##uart_option_combo", uart_options_sublabels[k][*curr_options_sel[k]], ImGuiComboFlags_NoArrowButton)) {
            POP_COLOR
            ImGui::Selectable(uart_options_headers[k], false, ImGuiSelectableFlags_Disabled);
            for(int n=0; n < sublabels_counts[k]; n++) {
                if(ImGui::Selectable(uart_options_sublabels[k][n], *curr_options_sel[k]==n)) {
                    uart_changed = true;
                    *curr_options_sel[k]=n;
                }
            }
            ImGui::EndCombo();
        }
        POP_COLOR
        p0 = ImGui::GetItemRectMin() + style.FramePadding;
        p1 = ImGui::GetItemRectMax() - style.FramePadding;
        draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2(p0.x,p1.y), p1, label_col);
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    ImGui::NewLine();
    ImGui::PopStyleColor();

    if(uart_changed)
        librador_set_uart_decode_settings(ch, 
                (UartSettings)
                {.decode_on=curr_ch_uart_settings->decode_on, .baudRate=static_cast<double>(baud_rates[curr_ch_uart_settings->baud_idx_sel]), .parity=parities[curr_ch_uart_settings->parity_idx_sel]});

    ImGui::PopID(); //ch
    return return_val;
}

void logicDecodeUI::print_stream(int id, const char * text, bool *at_bottom, float window_content_width, float ch_console_height)
{
    ImGui::PushID(id);
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
    if (ImGui::BeginChild("console",ImVec2(window_content_width, ch_console_height ), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::TextWrapped("%s", text);
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiWindow* window = g.CurrentWindow;

        ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
        bool scrolling = ImGui::ScrollWhenDraggingAnywhere(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
        if(!scrolling && *at_bottom){
            ImGui::SetScrollY(window, ImGui::GetScrollMaxY());
        }
        if (ImGui::GetScrollMaxY() == window->Scroll.y)
            *at_bottom=true;
        else
            *at_bottom=false;

    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

float logicDecodeUI::get_grabber_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    return 2 * style.ItemSpacing.y + 2 * style.FramePadding.y + ImGui::GetFontSize();
}

void logicDecodeUI::draw_console(float window_content_width)
{
    ImGuiStyle& style = ImGui::GetStyle();
    bool parity_check;
    float grabber_height = get_grabber_height();
    float y_avail = ImGui::GetContentRegionAvail().y;
    if(protocol_sel == Protocol::UART) {
        for(int i: {0,1}) {
            ch_console_height[i] *= both_ch_uart_settings[i].decode_on;
        }
        for(int i:{1,0}) {
            if(both_ch_uart_settings[i].decode_on) {
                float clamped_console_height = fmin(ch_console_height[i], y_avail - ch_console_height[(i+1)%2] - grabber_height * (1 + both_ch_uart_settings[(i+1)%2].decode_on) - style.ItemSpacing.y);
                clamped_console_height = fmax(clamped_console_height, 2 * grabber_height);
                grabber2_backlog += ch_console_height[i] - clamped_console_height; // note: the grabber is only ever changing one of the console heights, so grabber2_backlog will only ever be incremented for one of the consoles
                ch_console_height[i] = clamped_console_height;
            } 
        }
        if(both_ch_uart_settings[0].decode_on)
        {
            print_stream(1,librador_get_uart_string(1, &parity_check), &uart_ch_console_at_bottom[0], window_content_width, ch_console_height[0]);
        }
        float next_ch1_height = ch_console_height[1];
        if(both_ch_uart_settings[0].decode_on && both_ch_uart_settings[1].decode_on)
        {
            float console_sep_delta = draw_grabber(grabber_height, "chA_chB_splitter", &grabber1_backlog, 1, parity_check);
            float clamped_console_sep_delta = fmin(console_sep_delta, (ch_console_height[1] - 2 * grabber_height));
            clamped_console_sep_delta = fmax(clamped_console_sep_delta, -(ch_console_height[0] - 2 * grabber_height));
            next_ch1_height -= clamped_console_sep_delta;
            ch_console_height[0] += clamped_console_sep_delta;
            grabber1_backlog += console_sep_delta - clamped_console_sep_delta;
        }
        if(both_ch_uart_settings[1].decode_on)
        {
            print_stream(2, librador_get_uart_string(2, &parity_check), &uart_ch_console_at_bottom[1], window_content_width, ch_console_height[1]);
            ch_console_height[1] = next_ch1_height;
        }
    } else if(protocol_sel == Protocol::I2C) {
        ch_console_height[1] = 0.f;
        float clamped_console_height = fmin(ch_console_height[0], y_avail - grabber_height - style.ItemSpacing.y);
        clamped_console_height = fmax(clamped_console_height, 2 * grabber_height);
        grabber2_backlog += ch_console_height[0] - clamped_console_height;
        ch_console_height[0] = clamped_console_height;
        print_stream(3, librador_get_i2c_string(), &i2c_console_at_bottom, window_content_width, ch_console_height[0]);
    }
    float console_height_delta = draw_grabber(grabber_height, "plot_console_splitter", &grabber2_backlog, both_ch_uart_settings[1].decode_on ? 2 : 1, parity_check);
    if(both_ch_uart_settings[1].decode_on) {
        ch_console_height[1] += console_height_delta;
    } else if (both_ch_uart_settings[0].decode_on || (protocol_sel == Protocol::I2C)) {
        ch_console_height[0] += console_height_delta;
    }
}

bool logicDecodeUI::decoding_on()
{
    return both_ch_uart_settings[0].decode_on || both_ch_uart_settings[1].decode_on || protocol_sel == Protocol::I2C;
}

void logicDecodeUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    ImGuiStyle& style = ImGui::GetStyle();
    float grabber_height = get_grabber_height();
    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded)
    {
        ImGui::EndGroup();
        return;
    }

    bool logic_enable[2];
    if(inputs_ui->scopelogic_mode()) {
        logic_enable[0] = false;
        logic_enable[1] = true;
    } else {
        memcpy(logic_enable, inputs_ui->logic_enable, 2 * sizeof(bool));
    }
    bool i2c_changed = false;
    bool uart_allowed = logic_enable[0] || logic_enable[1];
    bool i2c_allowed = logic_enable[0] && logic_enable[1] && !both_ch_uart_settings[0].decode_on && !both_ch_uart_settings[1].decode_on;


    ImGui::BeginDisabled(!(logic_enable[0] || logic_enable[1])); //covers nearly entire fn.

    Protocol prots[2] = {Protocol::UART, Protocol::I2C};
    const char * labels[2] = {"UART", "I2C"};

    bool open_ch_serial_settings = false;
    char chAB[2] = {'A', 'B'};

    ImGui::BeginGroup(); // for bounding rect
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.f,0.f});
    ImGui::Dummy(ImVec2(width_pixels,0.f));
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2((width_pixels - ImGui::CalcTextSize("UART").x)/2.,style.FramePadding.y));
    ImGui::Text("UART");
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2( (width_pixels - ImGui::CalcTextSize("CH ACH B").x - style.ItemSpacing.x - 4 * style.FramePadding.x)/2., 0.f ));
    for (int ch: {1,2})
    {
        ImGui::BeginDisabled(!logic_enable[ch-1] || !(protocol_sel==Protocol::UART));
        char buf[20];
        sprintf(buf,"CH %c##serial_decode",chAB[ch-1]);
        bool need_pop = false;
        if(both_ch_uart_settings[ch-1].decode_on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonHovered));
            need_pop = true;
        }
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_Button));
        if(ImGui::Button(buf)) {
            both_ch_uart_settings[ch-1].decode_on = !both_ch_uart_settings[ch-1].decode_on;
            if (both_ch_uart_settings[ch-1].decode_on) {
                ch_console_height[ch-1] = init_console_height_per_ch - grabber_height;
            } else {
                ch_console_height[ch-1] = 0.f;
            }
            librador_set_uart_decode_settings(ch, 
                    (UartSettings)
                    {.decode_on=both_ch_uart_settings[ch-1].decode_on, .baudRate=static_cast<double>(baud_rates[both_ch_uart_settings[ch-1].baud_idx_sel]), .parity=parities[both_ch_uart_settings[ch-1].parity_idx_sel]});
        }
        ImGui::PopStyleColor();
        if(need_pop) {
            ImGui::PopStyleColor();
        }
        ImGui::EndDisabled(); 
        ImGui::SameLine();
    }
//     draw_list->AddLine(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(width_pixels,0.f), IM_COL32(90, 90, 120, 255));
//     
//     ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2( (width_pixels - ImGui::CalcTextSize("I2C").x - style.ItemInnerSpacing.x - CHECKBOX_SIZE)/2., style.FramePadding.y ));
//     ImGui::BeginDisabled(!i2c_allowed);
//     if(ImGui::Checkbox("I2C", (bool *) &protocol_sel)) {
//         i2c_changed = true;
//     }
//     ImGui::EndDisabled();

    ImGui::EndGroup();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax() + ImVec2(0.f,style.FramePadding.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(p0, p1, IM_COL32(90, 90, 120, 255));
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0.f,style.FramePadding.y - style.ItemSpacing.y));
    ImGui::Dummy({0.f,0.f}); // prevents issue with this draw() command affecting the vertical alignment of whatever ui element comes after it
    ImGui::EndGroup();

    ImGui::EndDisabled(); //!logic_enable[0] && !logic_enable[1]);
//     if(i2c_changed)
//     {
//         librador_set_i2c_is_decoding(protocol_sel == Protocol::I2C);
//         if(protocol_sel == Protocol::I2C)
//             ch_console_height[0] = init_console_height_per_ch - grabber_height;
//     }
}

void logicDecodeUI::update(inputsUI* inputs)
{
    bool logic_enable[2];
    if(inputs->scopelogic_mode()) {
        logic_enable[0] = false;
        logic_enable[1] = true;
    } else {
        memcpy(logic_enable, inputs->logic_enable, 2 * sizeof(bool));
    }
    for (int ch : {1,2})
    {
        uart_settings* curr_ch_uart_settings = &both_ch_uart_settings[ch-1];
        if((!logic_enable[ch-1] || !(protocol_sel==Protocol::UART)) && curr_ch_uart_settings->decode_on) 
        {
            curr_ch_uart_settings->decode_on = false; 
            librador_set_uart_decode_settings(ch, 
                    (UartSettings)
                    {.decode_on=curr_ch_uart_settings->decode_on, .baudRate=static_cast<double>(baud_rates[curr_ch_uart_settings->baud_idx_sel]), .parity=parities[curr_ch_uart_settings->parity_idx_sel]});
        }
    }
    if((!logic_enable[0] || !logic_enable[1]) && (protocol_sel==Protocol::I2C))
    {
        protocol_sel=Protocol::UART; //with ch_uart_settings->decode_on = false for both channels, so effectively disabling decoding;
        librador_set_i2c_is_decoding(false);
    }

}

int logicDecodeUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    int calc_height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                      style.FramePadding.y + ImGui::GetFontSize() + style.ItemSpacing.y + \
                      2 * style.FramePadding.y + ImGui::GetFontSize() + 2 * style.ItemSpacing.y;
    return calc_height;
//                       3 * style.FramePadding.y + ImGui::GetFontSize() + // for i2c checkbox
}

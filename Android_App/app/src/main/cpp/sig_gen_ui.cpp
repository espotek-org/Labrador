#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "librador.h"
#include "custom_imgui.h"
#include <chrono>
#include "imgui_internal.h"
#include "sig_gen_ui.h"
#include "inputs_ui.h"


void sigGenUI::amp_or_min_slider_and_button(const char* slider_label, const char* button_label, float *amp_or_min, float *amp_or_min_delayed, float *min_or_amp, float *min_or_amp_delayed) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushItemWidth(-1); 
    if(ImGui::custom_SliderFloat(slider_label, "V",  amp_or_min, 0.f, 9.6 - *min_or_amp_delayed, "%.2f V") || ImGui::IsItemDeactivatedAfterEdit()) {
        need_usb_send = true;
        if(ImGui::IsItemDeactivatedAfterEdit()) {
            *amp_or_min = std::min(*amp_or_min, 9.6f);
            *amp_or_min = std::max(*amp_or_min, 0.f);
            *min_or_amp = std::min(*min_or_amp, 9.6f - *amp_or_min);//only can have an effect on manual input
            *min_or_amp_delayed = *min_or_amp;
            *amp_or_min_delayed = *amp_or_min;
        }
    }
    ImGui::TableNextColumn();
    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(style.CellPadding.x,0.f));
    ButtonForSlider(button_label, slider_label, ImVec2(ImGui::GetContentRegionAvail().x + style.CellPadding.x,0.f));
}

void sigGenUI::draw(float width_pixels, inputsUI* inputs_ui)
{
    bool ch2_disabled = inputs_ui->logic_on();
    ImGuiStyle& style = ImGui::GetStyle();
    if(ch2_disabled) {
        both_ch_data[1] = ch_data();
    }

    ImGui::BeginGroup();
    standard_header(width_pixels);
    if(!is_expanded)
    {
        ImGui::EndGroup();
        return;
    }

    ImDrawList* draw_list;
    ImVec2 p0;
    ImVec2 p1;

    if (ImGui::BeginTable("sg_table", 2, ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg , ImVec2(width_pixels,0.f)))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::RadioButton("CH1", &ch_sel, 1); ImGui::SameLine();
        ImGui::RadioButton("CH2", &ch_sel, 2); 
        ImGui::BeginDisabled(ch2_disabled && ch_sel == 2);
        curr_ch_data = &both_ch_data[ch_sel-1];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-1); 
        if(ImGui::Combo("##combo3",  &curr_ch_data->wf, wf_names, IM_COUNTOF(wf_names)))
            need_usb_send = true;
        
        ImGui::TableNextColumn();
        ImGui::Text("Type");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-1); 
        float freq_conv = curr_ch_data->freq / freq_slider_bases[curr_ch_data->slider_base];
        if(ImGui::custom_SliderFloat("##sg_freq_slider", freq_slider_suffixs[curr_ch_data->slider_base],  &freq_conv, 0.0f, freq_slider_maxima[curr_ch_data->slider_base] / freq_slider_bases[curr_ch_data->slider_base], freq_slider_formats[curr_ch_data->slider_base]) 
                || ImGui::IsItemDeactivatedAfterEdit())
        {
            need_usb_send = true;
            curr_ch_data->freq = freq_conv * freq_slider_bases[curr_ch_data->slider_base];
            if(ImGui::IsItemDeactivatedAfterEdit()) {
                curr_ch_data->freq = std::min(curr_ch_data->freq, freq_slider_maxima[n_bases-1]);
                curr_ch_data->freq = std::max(curr_ch_data->freq, 0.f);
                if(curr_ch_data->freq <= freq_slider_maxima[0]) {
                    curr_ch_data->slider_base = 0;
                } else {
                    int i;
                    for(i=1;i<n_bases;i++) {
                        if((freq_slider_maxima[i] >= curr_ch_data->freq) && (freq_slider_maxima[i-1] < curr_ch_data->freq)) // find optimal freq scale to use for the slider given the value of the manually input freq.
                            break;
                    }
                    curr_ch_data->slider_base=i;
                }
            }
        }

        ImGui::TableNextColumn();

        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() - ImVec2(style.CellPadding.x,0.f));
        if(ButtonForSlider("Freq.", "##sg_freq_slider", ImVec2(ImGui::GetContentRegionAvail().x + style.CellPadding.x,0.f))) { // short press on button
            curr_ch_data->slider_base = (curr_ch_data->slider_base + 1) % n_bases; // toggle frequency base
            curr_ch_data->freq = std::min(curr_ch_data->freq, freq_slider_maxima[curr_ch_data->slider_base]); 
        }

        amp_or_min_slider_and_button("##sg_amp_slider", "Amp.", &curr_ch_data->amp, &curr_ch_data->amp_delayed, &curr_ch_data->min_val, &curr_ch_data->min_val_delayed);
        amp_or_min_slider_and_button("##sg_min_slider", "Min.", &curr_ch_data->min_val, &curr_ch_data->min_val_delayed, &curr_ch_data->amp, &curr_ch_data->amp_delayed);

        ImGui::EndDisabled(); // ch_sel==2 && ch2_disabled

        ImGui::EndTable();
    }
    ImGui::EndGroup();
    if(need_usb_send) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_usb_send) > between_usb_sends_min) {
            usb_send_data(ch_sel);
            last_usb_send = now;
            need_usb_send = false;
        }
    }
}

void sigGenUI::usb_send_data(int ch)
{
    librador_send_wave(both_ch_data[ch-1].wf, ch, both_ch_data[ch-1].freq, both_ch_data[ch-1].amp, both_ch_data[ch-1].min_val);
}

int sigGenUI::get_height()
{
    ImGuiStyle& style = ImGui::GetStyle();
    int calc_height = 2 * style.ItemSpacing.y + ImGui::GetFontSize() + \
                      CHECKBOX_SIZE + 2 * style.CellPadding.y + \
                      4 * (ImGui::GetFontSize() + 2 * style.FramePadding.y + 2 * style.CellPadding.y);
    return calc_height;
}

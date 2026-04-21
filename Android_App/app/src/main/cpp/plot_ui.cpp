#define IMGUI_DEFINE_MATH_OPERATORS
#include "implot.h"
#include "imgui_internal.h"
#include "implot_internal.h"
#include "plot_ui.h"
#include "librador.h"

void plotUI::recompute_x_bounds(bool mode_changed, inputsUI::Mode mode)
{
    if(mode_changed)
    {
        if(mode==inputsUI::Mode::Scope750) {
            time_window = std::min(5., time_window);
            delay = std::min(5. - time_window, delay);
            ImPlot::SetNextAxisLimits(ImAxis_X1, -(delay+time_window), -delay, ImPlotCond_Always);
            x_constraint_min = -5.;
            x_constraint_max = 0.;
//         } else if (xy) {
//             xmin = ymin;
//             xmax = ymax;
//             ImPlot::SetNextAxisLimits(ImAxis_X1, xmin, xmax, ImPlotCond_Always);
//             x_constraint_min = -20.;
//             x_constraint_max = 20.;
        } else {
            time_window = std::min(max_time_window_375khz, time_window);
            delay = std::min(max_time_window_375khz - time_window, delay);
            ImPlot::SetNextAxisLimits(ImAxis_X1, -(delay+time_window), -delay, ImPlotCond_Always);
            x_constraint_min = -max_time_window_375khz;
            x_constraint_max = 0.;
        }
    } else {
//         if(!xy) {
            delay = -xmax;
            time_window = (xmax - xmin);
//         }
    }
}

void get_ref_line_label(char * label, int size, char X_or_Y, ImPlotAxis ax, double ref_a, double ref_b) {

    double ref_1 = ImMin(ref_a, ref_b);
    double ref_2 = ImMax(ref_a, ref_b);
    int n_prec = 3;
    int n_sig_figs_needed = ref_1 != ref_2 ? ImMax(ImLog10(ImMax(ImAbs(ref_1),ImAbs(ref_2)) / ImAbs(ref_2-ref_1)),0.) + n_prec : 0;
    int n_sig_figs_needed_1 = (int) (floor(ImLog10(ImAbs(ref_1))) - floor(ImLog10(ax.Range.Max - ax.Range.Min))) + n_prec;
    int n_sig_figs_needed_2 = (int) (floor(ImLog10(ImAbs(ref_2))) - floor(ImLog10(ax.Range.Max - ax.Range.Min))) + n_prec;

    n_sig_figs_needed_1 = ImMax(n_sig_figs_needed, n_sig_figs_needed_1);
    n_sig_figs_needed_2 = ImMax(n_sig_figs_needed, n_sig_figs_needed_2);
    n_sig_figs_needed_1 = ImMin(n_sig_figs_needed_1, 8);
    n_sig_figs_needed_2 = ImMin(n_sig_figs_needed_2, 8);

    // write the ref 1, 2 values and their difference to the same number of decimal places
    int ref_1_prec = n_sig_figs_needed_1 - floor(ImLog10(ImAbs(ref_1)));
    int ref_2_prec = n_sig_figs_needed_2 - floor(ImLog10(ImAbs(ref_2)));
    ref_1_prec = ImMax(ref_1_prec, ref_2_prec);
    ref_2_prec = ImMax(ref_1_prec, ref_2_prec);
    n_sig_figs_needed_1 = ref_1_prec + floor(ImLog10(ImAbs(ref_1)));
    n_sig_figs_needed_2 = ref_2_prec + floor(ImLog10(ImAbs(ref_2)));
    double difference = ref_2 - ref_1;
    int n_sig_figs_needed_diff = ImMax(ref_1_prec, ref_2_prec) + floor(ImLog10(difference));
    n_sig_figs_needed_diff = ImMin(n_sig_figs_needed_diff, 8);

    int buf_size = 64;
    char str_to_format[buf_size];
    ImFormatString(str_to_format, buf_size, "%%c1: %%.%dg\n%%c2: %%.%dg\n\xee\xa4\x84%%c: %%.%dg", n_sig_figs_needed_1, n_sig_figs_needed_2, n_sig_figs_needed_diff);

    ImFormatString(label, size, str_to_format, X_or_Y, ref_1, X_or_Y, ref_2, X_or_Y, difference);
}

void plotUI::draw(bool iso_thread_active, inputsUI::Mode mode, bool chA_enabled, bool chB_enabled, double data_width, double plot_height)
{
    std::vector<double> *from_librador_chA;
    std::vector<double> *from_librador_chB;
    std::vector<double> blank_data{};
    std::vector<double> time_array;

    if(iso_thread_active){
        time_array = librador_get_time_array(delay, time_window, GRAPH_SAMPLES);
        switch(mode) {
        case inputsUI::Mode::Ch1Scope:
            from_librador_chA = librador_get_analog_data(1,time_window,GRAPH_SAMPLES,delay, 0);
            break;
        case inputsUI::Mode::ScopeLogic:
            from_librador_chA = librador_get_analog_data(1,time_window,GRAPH_SAMPLES,delay, 0);
            from_librador_chB = librador_get_digital_data(2,time_window,GRAPH_SAMPLES,delay);
            break;
        case inputsUI::Mode::ScopeScope:
            from_librador_chA = librador_get_analog_data(1,time_window,GRAPH_SAMPLES,delay, 0);
            from_librador_chB = librador_get_analog_data(2,time_window,GRAPH_SAMPLES,delay, 0);
            break;
        case inputsUI::Mode::Ch1Logic:
            from_librador_chA = librador_get_digital_data(1,time_window,GRAPH_SAMPLES,delay);
            break;
        case inputsUI::Mode::LogicLogic:
            from_librador_chA = librador_get_digital_data(1,time_window,GRAPH_SAMPLES,delay);
            from_librador_chB = librador_get_digital_data(2,time_window,GRAPH_SAMPLES,delay);
            break;
        case inputsUI::Mode::None:
            break;
        case inputsUI::Mode::Scope750:
            from_librador_chA = librador_get_analog_data(1,time_window,GRAPH_SAMPLES,delay, 0);
            break;
        case inputsUI::Mode::Multimeter:
            from_librador_chA = librador_get_analog_data(1,time_window,GRAPH_SAMPLES,delay, 0);
            break;
        }
    } else {
        from_librador_chA = &blank_data;
        from_librador_chB = &blank_data;
        time_array = blank_data;
    }

    ImGui::BeginChild("plot",ImVec2(data_width, plot_height));
    {
        if (ImPlot::BeginPlot("##scope traces", ImGui::GetContentRegionAvail(),ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes("time (s)","volts");

            ImPlot::SetupAxisFormat(ImAxis_X1, ImPlot::Formatter_Offset_Plus_Delta, (void*) "%g");
            ImPlot::SetupAxisFormat(ImAxis_Y1, ImPlot::Formatter_Offset_Plus_Delta, (void*) "%g");
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, x_constraint_min, x_constraint_max);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, -max_voltage, max_voltage);
            ImPlot::SetupAxesLimits(xmin, xmax, ymin, ymax, ImPlotCond_Once);

            ImPlotSpec spec = ImPlotSpec();
            spec.LineWeight = 2;
            if(chA_enabled)
                ImPlot::PlotLine("CH A", time_array.data(), from_librador_chA->data(), from_librador_chA->size(), spec);
            if(chB_enabled)
                ImPlot::PlotLine("CH B", time_array.data(), from_librador_chB->data(), from_librador_chB->size(), spec);

            ImGuiContext& g = *GImGui;

            if(ImPlot::IsAxisActivated(ImAxis_X1)) {
                float mouse_down_clicked_val = ImPlot::getMouseDownClickedVal(ImAxis_X1);
                cursor_drag_tool_toggle = fabs(x_ref_1 - mouse_down_clicked_val) > fabs(x_ref_2 - mouse_down_clicked_val);
            }

            ImPlotDragToolFlags x1_drag_tool_flags = cursor_drag_tool_toggle ? ImPlotDragToolFlags_NoAxisInputs : 0;
            ImPlotDragToolFlags x2_drag_tool_flags = cursor_drag_tool_toggle ? 0 : ImPlotDragToolFlags_NoAxisInputs;

            if(ImPlot::IsAxisClicked(ImAxis_X1)) {
                if(!enable_x_ref_lines) {
                    x_ref_1 = ImPlot::getMouseDownClickedVal(ImAxis_X1);
                    x_ref_2 = ImPlot::getMouseDownClickedVal(ImAxis_X1);
                }
                enable_x_ref_lines = true;
                if(g.IO.MouseClickedLastCount[0]==2) {
                    enable_x_ref_lines = false;
                }
            }

            if(enable_x_ref_lines) {
                ImPlot::DragLineX(0,&x_ref_1,ImVec4(1,1,1,1), 1.f, x1_drag_tool_flags);
                ImPlot::DragLineX(1,&x_ref_2,ImVec4(1,1,1,1), 1.f, x2_drag_tool_flags);
            }

            if(ImPlot::IsAxisActivated(ImAxis_Y1)) {
                float mouse_down_clicked_val = ImPlot::getMouseDownClickedVal(ImAxis_Y1);
                cursor_drag_tool_toggle = fabs(y_ref_1 - mouse_down_clicked_val) > fabs(y_ref_2 - mouse_down_clicked_val);
            }

            ImPlotDragToolFlags y1_drag_tool_flags = cursor_drag_tool_toggle ? ImPlotDragToolFlags_NoAxisInputs : 0;
            ImPlotDragToolFlags y2_drag_tool_flags = cursor_drag_tool_toggle ? 0 : ImPlotDragToolFlags_NoAxisInputs;

            if(ImPlot::IsAxisClicked(ImAxis_Y1)) {
                if(!enable_y_ref_lines) {
                    y_ref_1 = ImPlot::getMouseDownClickedVal(ImAxis_Y1);
                    y_ref_2 = ImPlot::getMouseDownClickedVal(ImAxis_Y1);
                }
                enable_y_ref_lines = true;
                if(g.IO.MouseClickedLastCount[0]==2) {
                    enable_y_ref_lines = false;
                }
            }

            if(enable_y_ref_lines) {
                ImPlot::DragLineY(0,&y_ref_1,ImVec4(1,1,1,1), 1.f, y1_drag_tool_flags);
                ImPlot::DragLineY(1,&y_ref_2,ImVec4(1,1,1,1), 1.f, y2_drag_tool_flags);
            }

            ImPlotRect axes_limits = ImPlot::GetPlotLimits();
            if(ImPlot::IsAxisLongPressed(ImAxis_X1)) {
                ImGui::SetNextWindowPos(ImGui::GetWindowPos() + ImGui::GetWindowSize()/2.,0,{0.5,0.5});
                ImGui::OpenPopup("select x lims");
            }
            if(ImGui::BeginPopup("select x lims")) {
                ImGuiStyle& style = ImGui::GetStyle();
                ImGui::Text("X-axis limits:");
                ImGui::PushItemWidth(ImGui::CalcTextSize("-10.00 s").x + 2 * style.FramePadding.x);
                ImGui::InputFloat("Min", &xmin, 0.f, 0.f, "%.2f s");
                float new_window = xmax - xmin;
                float new_delay = fabs(-xmax); // fabs to prevent signed 0
                ImGui::InputFloat("Window", &new_window, 0.f, 0.f, "%.2f s");
                ImGui::InputFloat("Delay", &new_delay, 0.f, 0.f, "%.2f s");
                xmin = -new_delay - new_window;
                xmax = -new_delay;
                ImGui::EndPopup();
            } else {
                xmin = axes_limits.X.Min;
                xmax = axes_limits.X.Max;
            }

            if(ImPlot::IsAxisLongPressed(ImAxis_Y1)) {
                ImGui::SetNextWindowPos(ImGui::GetWindowPos() + ImGui::GetWindowSize()/2.,0,{0.5,0.5});
                ImGui::OpenPopup("select y lims");
            }
            if(ImGui::BeginPopup("select y lims")) {
                ImGuiStyle& style = ImGui::GetStyle();
                ImGui::Text("Y-axis limits:");
                ImGui::PushItemWidth(ImGui::CalcTextSize("-20.00 V").x + 2 * style.FramePadding.x);
                ImGui::InputFloat("Max", &ymax, 0.f, 0.f, "%.2f V");
                ImGui::InputFloat("Min", &ymin, 0.f, 0.f, "%.2f V");
                ImGui::EndPopup();
            } else {
                ymin = axes_limits.Y.Min;
                ymax = axes_limits.Y.Max;
            }

            ImPlotPlot* mainplot = ImPlot::GetCurrentPlot();
            ImPlot::EndPlot();

            ImPlot::SetNextAxisLimits(ImAxis_X1, xmin, xmax, ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, ymin, ymax, ImPlotCond_Always);

            int buffer_size = 64;
            char x_ref_line_label[buffer_size];
            char y_ref_line_label[buffer_size];
            if(enable_x_ref_lines) {
                get_ref_line_label(x_ref_line_label, buffer_size, 'X', mainplot->XAxis(0), x_ref_1, x_ref_2);
            } else {
                x_ref_line_label[0] = '\0';
            }
            if(enable_y_ref_lines) {
                get_ref_line_label(y_ref_line_label, buffer_size, 'Y', mainplot->YAxis(0), y_ref_1, y_ref_2);
            } else {
                y_ref_line_label[0] = '\0';
            }


            if(enable_x_ref_lines || enable_y_ref_lines) {
                ImGuiStyle& style = ImGui::GetStyle();

                ImPlotContext& gp = *GImPlot;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2 * style.ItemSpacing.x, style.ItemSpacing.y});
                float ref_legend_width = ImGui::CalcTextSize(x_ref_line_label).x + ImGui::CalcTextSize(y_ref_line_label).x + (enable_x_ref_lines && enable_y_ref_lines) * style.ItemSpacing.x + 2 * style.FramePadding.x;
                float ref_legend_height = ImMax(ImGui::CalcTextSize(x_ref_line_label).y, ImGui::CalcTextSize(y_ref_line_label).y) + 2 * style.FramePadding.y;

                ImGui::SetCursorScreenPos(mainplot->PlotRect.Max - ImVec2(ref_legend_width, ref_legend_height) - gp.Style.LegendPadding );
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRectFilled(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(ref_legend_width, ref_legend_height), ImGui::GetColorU32(ImGuiCol_WindowBg,.75));
                ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + style.FramePadding);

                ImGui::BeginGroup();
                if(enable_x_ref_lines) {
                    ImGui::Text("%s", x_ref_line_label);
                }
                if(enable_x_ref_lines && enable_y_ref_lines) {
                    ImGui::SameLine();
                }
                if(enable_y_ref_lines) {
                    ImGui::Text("%s", y_ref_line_label);
                }
                ImGui::EndGroup();
                ImGui::PopStyleVar();
            }

        }
    }
    ImGui::EndChild();
}

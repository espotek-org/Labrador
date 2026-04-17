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

            if(enable_x_ref_lines || enable_y_ref_lines) {
                ImGui::SetNextWindowBgAlpha(0.25f);
                ImGuiStyle& style = ImGui::GetStyle();

                ImPlotContext& gp = *GImPlot;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2 * style.ItemSpacing.x, style.ItemSpacing.y});
                float ref_legend_width = (enable_x_ref_lines + enable_y_ref_lines) * ImGui::CalcTextSize("X1: -0.000").x + (enable_x_ref_lines && enable_y_ref_lines) * style.ItemSpacing.x + 2 * style.FramePadding.x;
                float ref_legend_height = 3 * ImGui::GetFontSize() + 2 * style.FramePadding.y;
                ImGui::SetCursorScreenPos(mainplot->PlotRect.Max - ImVec2(ref_legend_width, ref_legend_height) - gp.Style.LegendPadding );
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRectFilled(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(ref_legend_width, ref_legend_height), ImGui::GetColorU32(ImGuiCol_WindowBg,.75));
                ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + style.FramePadding);

                ImGui::BeginGroup();
                if(enable_x_ref_lines) {
                    ImGui::Text("X1: %.3f\nX2: %.3f\n\xee\xa4\x84X: %.3f", fmin(x_ref_1,x_ref_2), fmax(x_ref_1,x_ref_2), fabs(x_ref_2 - x_ref_1));
                }
                if(enable_x_ref_lines && enable_y_ref_lines) {
                    ImGui::SameLine();
                }
                if(enable_y_ref_lines) {
                    ImGui::Text("Y1: %.3f\nY2: %.3f\n\xee\xa4\x84Y: %.3f", fmin(y_ref_1,y_ref_2), fmax(y_ref_1,y_ref_2), fabs(y_ref_2 - y_ref_1));
                }
                ImGui::EndGroup();
                ImGui::PopStyleVar();
            }

        }
    }
    ImGui::EndChild();
}

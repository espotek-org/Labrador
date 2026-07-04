// Desktop layout — faithful port of the Monash LabraScope arrangement
// (iterated with several dozen students; the tested reference).
// 60% plot on the left, 40% control column on the right, horizontal scroll
// fallback when the controls would drop under min_widget_width.
#include "ui/desktop/DesktopFrontend.h"
#include "app/App.h"

void DesktopFrontend::renderLayout(App& app)
{
    const int padding = 6;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    float base_window_width = display_size.x;

    float plot_width = (base_window_width - 2 * style.WindowPadding.x) * 0.60f - padding;
    float right_col_width
        = base_window_width - plot_width - 2 * padding - 2 * style.WindowPadding.x;

    float content_width = base_window_width;
    if (right_col_width < min_widget_width)
    {
        float deficit = min_widget_width - right_col_width;
        content_width = base_window_width + deficit;
    }

    ImGui::SetNextWindowContentSize(ImVec2(content_width, 0.0f));

    float predicted_plot_width = (display_size.x - 2 * style.WindowPadding.x) * 0.60f - padding;
    float predicted_right_width
        = display_size.x - predicted_plot_width - 2 * padding - 2 * style.WindowPadding.x;

    bool need_h_scroll = (predicted_right_width < min_widget_width);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);

    ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar;

    if (need_h_scroll)
        main_flags |= ImGuiWindowFlags_HorizontalScrollbar;

    ImGui::Begin("Main Window", NULL, main_flags);

    RenderMenuBar(app);

    style.WindowPadding = ImVec2(padding, padding);

    ImVec2 window_size = ImGui::GetWindowSize();
    plot_width = (window_size.x - 2 * style.WindowPadding.x) * 0.60f - padding;
    float plot_height = (window_size.y - 2 * style.WindowPadding.y) * 1.00f - padding;

    // Left Column: Plot
    style.ItemSpacing = ImVec2(0, 0);
    int menu_height = ImGui::GetFrameHeight();
    ImGui::BeginChild("Left Column",
        ImVec2(plot_width, window_size.y - 2 * style.WindowPadding.y - menu_height), false);
    style.ItemSpacing = ImVec2(padding, padding);

    PlotWidgetObj.setSize(ImVec2(plot_width, plot_height));
    PlotWidgetObj.Render();

    ImGui::EndChild();

    // Right Column: Controls
    float control_widget_height
        = (window_size.y - 2 * style.WindowPadding.y - 3 * style.ItemSpacing.y) * 0.25f;

    float base_right_width
        = window_size.x - plot_width - 2 * padding - 2 * style.WindowPadding.x;
    float right_width = base_right_width;
    if (right_width < min_widget_width)
    {
        right_width = min_widget_width;
    }

    style.ItemSpacing = ImVec2(padding, 0);
    ImGui::SameLine();
    ImGui::BeginChild("Right Column",
        ImVec2(right_width, window_size.y - 2 * style.WindowPadding.y - menu_height), false);
    style.ItemSpacing = ImVec2(padding, padding);

    PSUWidget.setSize(ImVec2(0, control_widget_height));
    PSUWidget.Render();

    SG1Widget.setSize(ImVec2(0, control_widget_height));
    SG1Widget.Render();

    SG2Widget.setSize(ImVec2(0, control_widget_height));
    SG2Widget.Render();

    OSCWidget.Render();

    analysisToolsWidget.Render();

    InputsWidget.Render();
    MultimeterWidget.Render();
    LogicWidget.Render();
    DAQWidget.Render();
    DigitalOutWidget.Render();
    CalibrationWidget.Render();
    DAQReplayWidget.Render();

    UpdateHardwareState(app);

    ImGui::EndChild();
    ImGui::End();
}

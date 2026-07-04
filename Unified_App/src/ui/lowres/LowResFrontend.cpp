// Compact layout for 800x480-class LCDs (Raspberry Pi builds): plot on the
// left, tabbed control column on the right so everything fits 480px height.
#include "ui/lowres/LowResFrontend.h"
#include "app/App.h"

void LowResFrontend::renderLayout(App& app)
{
    const int padding = 4;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);

    ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar;

    ImGui::Begin("Main Window", NULL, main_flags);

    RenderMenuBar(app);

    style.WindowPadding = ImVec2(padding, padding);
    style.ItemSpacing = ImVec2(padding, padding);

    ImVec2 window_size = ImGui::GetWindowSize();
    int menu_height = ImGui::GetFrameHeight();
    float content_height = window_size.y - 2 * style.WindowPadding.y - menu_height;
    float plot_width = (window_size.x - 2 * style.WindowPadding.x) * 0.58f - padding;

    ImGui::BeginChild("Compact Plot", ImVec2(plot_width, content_height), false);
    PlotWidgetObj.setSize(ImVec2(plot_width, content_height));
    PlotWidgetObj.Render();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Compact Controls", ImVec2(0, content_height), false);
    if (ImGui::BeginTabBar("##compact_tabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        struct Tab { const char* label; ControlWidget* widget; };
        const Tab tabs[] = {
            { "Scope", &OSCWidget },
            { "SG1", &SG1Widget },
            { "SG2", &SG2Widget },
            { "PSU", &PSUWidget },
            { "Analysis", &analysisToolsWidget },
            { "Inputs", &InputsWidget },
            { "Meter", &MultimeterWidget },
            { "Logic", &LogicWidget },
            { "DAQ", &DAQWidget },
            { "DO", &DigitalOutWidget },
            { "Cal", &CalibrationWidget },
            { "Replay", &DAQReplayWidget },
        };
        for (const Tab& t : tabs)
        {
            if (ImGui::BeginTabItem(t.label))
            {
                ImGui::BeginChild("##compact_tab_content", ImVec2(0, 0), false);
                t.widget->setSize(ImVec2(0, 0));
                t.widget->Render();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    UpdateHardwareState(app);

    ImGui::EndChild();
    ImGui::End();
}

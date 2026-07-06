// Compact layout for 800x480-class LCDs (Raspberry Pi builds, usually with a
// touchscreen): plot on the left, tabbed control column on the right so
// everything fits 480px height.
#include "ui/lowres/LowResFrontend.h"
#include "app/App.h"
#include "instruments/UIComponents.hpp" // ScaledPx

#include <algorithm>

void LowResFrontend::renderLayout(App& app)
{
    // Touch pass (Chris, 2026-07-06: "compact doesn't seem touchscreen
    // friendly"): finger-sized frames, hit slop around every item, and fat
    // grabs/scrollbars — while staying inside the 480 px height budget.
    // Save/restore the whole style rather than mutating it: the old code
    // leaked its 4 px paddings into the other layouts on a live switch.
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle style_backup = style;
    style.WindowPadding = ImVec2(4, 4);
    style.ItemSpacing = ImVec2(6, 6);
    style.FramePadding = ImVec2(8, 6);         // frame height ~= font + 12 px
    style.TouchExtraPadding = ImVec2(4, 4);    // hit slop beyond the visuals
    style.GrabMinSize = ScaledPx(20.0f);       // draggable slider/scroll grabs
    style.ScrollbarSize = ScaledPx(18.0f);     // finger-draggable scrollbars

    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);

    ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar;

    ImGui::Begin("Main Window", NULL, main_flags);

    RenderMenuBar(app);

    ImVec2 window_size = ImGui::GetWindowSize();
    int menu_height = ImGui::GetFrameHeight();
    float content_height = window_size.y - 2 * style.WindowPadding.y - menu_height;
    // The control column gets 42% of the width, but never less than it needs
    // to render its content at the current text size (a proportional-only
    // split clipped combos once the text grew).
    const float avail_w = window_size.x - 2 * style.WindowPadding.x;
    const float panel_width
        = std::max(avail_w * 0.42f, std::min(ScaledPx(360.0f), avail_w * 0.6f));
    float plot_width = avail_w - panel_width - style.ItemSpacing.x;

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

    style = style_backup;
}

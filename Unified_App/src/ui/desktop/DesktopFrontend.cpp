// Desktop layout — 2026 ground-up redesign. One full-height plot, a real menu
// bar (Qt-desktop parity: exports, device control, gain menu, view toggles),
// a slim toolbar for the always-on controls, a single collapsible side panel
// whose pages are picked from a rail, and a bottom status bar. See the header
// for the ASCII sketch.
#include "ui/desktop/DesktopFrontend.h"
#include "app/App.h"
#include "app/settings.h"
#include "platform/file_dialog.h"
#include "librador.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ---- Small toolbar helpers --------------------------------------------------

// Button drawn in a solid colour (run/stop state button).
static bool SolidButton(const char* label, const ImVec4& col, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(std::min(col.x * 1.25f, 1.0f), std::min(col.y * 1.25f, 1.0f),
            std::min(col.z * 1.25f, 1.0f), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(col.x * 0.8f, col.y * 0.8f, col.z * 0.8f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// Button that stays highlighted while *v is on; click toggles.
static void ToolToggle(const char* label, bool* v, const ImVec2& size = ImVec2(0, 0))
{
    const bool on = *v;
    if (on)
        ImGui::PushStyleColor(
            ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label, size))
        *v = !*v;
    if (on)
        ImGui::PopStyleColor();
}

static void ToolbarSep()
{
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, 10.0f);
}

// Accent-tinted collapsing header for the side-panel sections.
static bool SectionHeader(const char* label, const float* accent)
{
    const ImVec4 c(accent[0], accent[1], accent[2], 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(c.x, c.y, c.z, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(c.x, c.y, c.z, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(c.x, c.y, c.z, 0.55f));
    bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);
    return open;
}

// ---- Panel metadata ---------------------------------------------------------

const char* DesktopFrontend::panelName(int p)
{
    static const char* names[PanelCount]
        = { "Scope", "Signals", "Meter", "Logic", "Record", "Analysis" };
    return names[p];
}

const float* DesktopFrontend::panelAccent(int p)
{
    static const float* accents[PanelCount] = { constants::OSC1_ACCENT,
        constants::SG1_ACCENT, constants::MATH_ACCENT,
        constants::SPECTRUM_ANALYSER_ACCENT, constants::NETWORK_ANALYSER_ACCENT,
        constants::SG2_ACCENT };
    return accents[p];
}

// ---- Settings ---------------------------------------------------------------

void DesktopFrontend::loadSettings(Settings& s)
{
    InstrumentFrontend::loadSettings(s);
    m_sidebar_visible = s.getBool("desk_panel_visible", true);
    const double page = s.getDouble("desk_panel_page", (double)PanelScope);
    if (page >= 0 && page < PanelCount)
        m_panel = (int)page;
    m_sidebar_width
        = std::clamp((float)s.getDouble("desk_panel_width", 440.0), 300.0f, 800.0f);
}

void DesktopFrontend::saveSettings(Settings& s)
{
    InstrumentFrontend::saveSettings(s);
    s.set("desk_panel_visible", m_sidebar_visible);
    s.set("desk_panel_page", (double)m_panel);
    s.set("desk_panel_width", (double)m_sidebar_width);
}

// ---- Shortcuts (desktop-only; the shared set lives in handleShortcuts) ------

void DesktopFrontend::handleDesktopShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive())
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_B, false))
        m_sidebar_visible = !m_sidebar_visible;
}

// ---- Layout -----------------------------------------------------------------

void DesktopFrontend::renderLayout(App& app)
{
    handleDesktopShortcuts();

    const ImVec2 display_size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display_size);

    const ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Main Window", NULL, main_flags);
    ImGui::PopStyleVar();

    renderDesktopMenuBar(app);

    const float toolbar_h = ImGui::GetFrameHeight() + 12.0f;
    renderToolbar(app, toolbar_h);

    // --- middle strip: plot | splitter | side panel | rail ---
    const ImGuiStyle& style = ImGui::GetStyle();
    const float status_h = ImGui::GetTextLineHeight() + 10.0f;
    const float middle_h
        = ImGui::GetContentRegionAvail().y - status_h - style.ItemSpacing.y;
    const float rail_w = ImGui::GetFontSize() * 5.0f;
    const float splitter_w = 6.0f;

    // Keep both the plot and the panel usable at any window size.
    const float avail_w = ImGui::GetContentRegionAvail().x;
    float max_panel = avail_w - rail_w - splitter_w - 320.0f;
    m_sidebar_width = std::clamp(m_sidebar_width, 300.0f, std::max(300.0f, max_panel));

    const float plot_w = avail_w - rail_w
        - (m_sidebar_visible ? m_sidebar_width + splitter_w : 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::BeginChild("##plotarea", ImVec2(plot_w, middle_h),
        ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    PlotWidgetObj.setSize(ImGui::GetContentRegionAvail());
    PlotWidgetObj.Render();
    ImGui::EndChild();

    if (m_sidebar_visible)
    {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##panel_splitter", ImVec2(splitter_w, middle_h));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive())
            m_sidebar_width -= ImGui::GetIO().MouseDelta.x;
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const float x = (mn.x + mx.x) * 0.5f;
            const ImU32 col = ImGui::GetColorU32(
                ImGui::IsItemActive() ? ImGuiCol_SeparatorActive
                                      : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
                                                               : ImGuiCol_Separator);
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(x, mn.y), ImVec2(x, mx.y), col, 1.0f);
        }

        ImGui::SameLine(0.0f, 0.0f);
        renderSidePanel(app, middle_h);
    }

    ImGui::SameLine(0.0f, 0.0f);
    renderRail(middle_h);

    renderStatusBar(app);

    UpdateHardwareState(app);

    ImGui::End();

    renderCalibrationWindow();
    renderAboutWindow();
}

// ---- Menu bar ---------------------------------------------------------------

void DesktopFrontend::exportCsvMenuItem(const char* item_label,
    const std::vector<double>& x, const std::vector<double>& y,
    const char* x_header, const char* y_header)
{
    const bool has_data = !x.empty() && x.size() == y.size();
    if (ImGui::MenuItem(item_label, NULL, false, has_data))
    {
        // Copy the data now — the dialog callback runs frames later, when the
        // live acquisition buffers have moved on.
        std::vector<double> xc = x, yc = y;
        std::string xh = x_header, yh = y_header;
        ShowSaveFileDialog("csv",
            [xc = std::move(xc), yc = std::move(yc), xh = std::move(xh),
                yh = std::move(yh)](const char* path) {
                if (path)
                    Export2ColToCsvFile(path, "csv", xc, yc, xh.c_str(), yh.c_str());
            });
    }
}

void DesktopFrontend::renderDesktopMenuBar(App& app)
{
    if (!ImGui::BeginMenuBar())
        return;

    // --- File ---
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::BeginMenu("Export"))
        {
            exportCsvMenuItem("OSC1 to CSV...", OSC1Data.GetTime(),
                OSC1Data.GetData(), "Time", "Voltage");
            exportCsvMenuItem("OSC2 to CSV...", OSC2Data.GetTime(),
                OSC2Data.GetData(), "Time", "Voltage");
            exportCsvMenuItem("Math to CSV...", MathData.GetTime(),
                MathData.GetData(), "Time", "Voltage");
            ImGui::Separator();
            exportCsvMenuItem("OSC1 spectrum to CSV...", spectrum_plots.osc1.x,
                spectrum_plots.osc1.y, "Frequency (Hz)", "Magnitude");
            exportCsvMenuItem("OSC2 spectrum to CSV...", spectrum_plots.osc2.x,
                spectrum_plots.osc2.y, "Frequency (Hz)", "Magnitude");
            ImGui::Separator();
            exportCsvMenuItem("Network magnitude to CSV...", network_plots.freq,
                network_plots.mag, "Frequency (Hz)", "Magnitude");
            exportCsvMenuItem("Network phase to CSV...", network_plots.freq,
                network_plots.phase, "Frequency (Hz)", "Phase (rads)");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open DAQ recording..."))
        {
            showPanel(PanelRecord);
            DAQReplayWidget.openFilePrompt();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))
            app.requestQuit();
        ImGui::EndMenu();
    }

    // --- Device ---
    if (ImGui::BeginMenu("Device"))
    {
        if (app.isFlashing())
            ImGui::TextColored(constants::GRAY_TEXT, "Firmware update in progress");
        else if (app.isConnected())
            ImGui::TextColored(constants::GRAY_TEXT, "Firmware %hu.%hhu — connected",
                librador_get_device_firmware_version(),
                librador_get_device_firmware_variant());
        else
            ImGui::TextColored(constants::GRAY_TEXT, "No Labrador board detected");
        ImGui::Separator();

        if (ImGui::BeginMenu("Input Mode"))
        {
            for (int i = 0; i < InputsControl::ModeCount; i++)
            {
                if (ImGui::MenuItem(InputsControl::modeLabel(i), NULL,
                        InputsWidget.selectedIndex() == i))
                    InputsWidget.selectIndex(i);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (ImGui::MenuItem("Calibration..."))
            m_show_calibration = true;
#ifndef __ANDROID__
        // Reuses the mismatch modal flow (App::drawFirmwarePopup opens it)
        if (ImGui::MenuItem("Reflash firmware", NULL, false,
                app.isConnected() && !app.isFlashing()))
            app.requestFirmwareReflash();
#endif
        if (ImGui::MenuItem("Reset USB connection", "Esc", false, app.isConnected()))
            librador_reset_usb();
        ImGui::EndMenu();
    }

    // --- Scope ---
    if (ImGui::BeginMenu("Scope"))
    {
        if (ImGui::MenuItem("Run", "Space", !OSCWidget.Paused))
            OSCWidget.Paused = !OSCWidget.Paused;
        if (ImGui::MenuItem("Auto-fit both axes", "F"))
        {
            OSCWidget.AutofitX = true;
            OSCWidget.AutofitY = true;
        }
        ImGui::Separator();

        // Qt parity: the Oscilloscope > Gain radio menu (Auto + 0.5x..64x)
        if (ImGui::BeginMenu("Hardware Gain"))
        {
            if (ImGui::MenuItem("Auto", NULL, OSCWidget.AutoGain))
                OSCWidget.AutoGain = !OSCWidget.AutoGain;
            ImGui::Separator();
            for (int i = 0; i < OSCControl::GainValueCount; i++)
            {
                char lab[16];
                snprintf(lab, sizeof lab, "%gx", OSCControl::GainValues[i]);
                if (ImGui::MenuItem(
                        lab, NULL, !OSCWidget.AutoGain && OSCWidget.GainComboCurrentItem == i))
                {
                    OSCWidget.GainComboCurrentItem = i;
                    OSCWidget.AutoGain = false;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();

        // Main-plot views — mutually exclusive, matching the Qt view actions
        if (ImGui::MenuItem("XY Mode", NULL, OSCWidget.XYMode))
        {
            OSCWidget.XYMode = !OSCWidget.XYMode;
            if (OSCWidget.XYMode)
                OSCWidget.EyeDiagram = false;
        }
        if (ImGui::MenuItem("Eye Diagram", NULL, OSCWidget.EyeDiagram))
        {
            OSCWidget.EyeDiagram = !OSCWidget.EyeDiagram;
            if (OSCWidget.EyeDiagram)
                OSCWidget.XYMode = false;
        }
        if (OSCWidget.EyeDiagram)
        {
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputInt("Eye traces", &OSCWidget.EyeTraces, 1, 8))
                OSCWidget.EyeTraces = std::clamp(OSCWidget.EyeTraces,
                    OSCControl::EyeTracesMin, OSCControl::EyeTracesMax);
        }
        ImGui::Separator();

        if (ImGui::MenuItem("Cursor 1", "1", OSCWidget.Cursor1toggle))
            OSCWidget.Cursor1toggle = !OSCWidget.Cursor1toggle;
        if (ImGui::MenuItem("Cursor 2", "2", OSCWidget.Cursor2toggle))
            OSCWidget.Cursor2toggle = !OSCWidget.Cursor2toggle;
        if (ImGui::MenuItem("Signal Properties", NULL, OSCWidget.SignalPropertiesToggle))
            OSCWidget.SignalPropertiesToggle = !OSCWidget.SignalPropertiesToggle;
        ImGui::EndMenu();
    }

    // --- Tools ---
    if (ImGui::BeginMenu("Tools"))
    {
        const bool spec_on
            = analysisToolsWidget.ToolsOn && analysisToolsWidget.CurrentTab == 0;
        if (ImGui::MenuItem("Spectrum Analyser", NULL, spec_on))
        {
            if (spec_on)
                analysisToolsWidget.ToolsOn = false;
            else
            {
                analysisToolsWidget.ToolsOn = true;
                analysisToolsWidget.CurrentTab = 0;  // plot switches immediately
                analysisToolsWidget.RequestedTab = 0; // tab bar follows when drawn
                showPanel(PanelAnalysis);
            }
        }
        const bool net_on
            = analysisToolsWidget.ToolsOn && analysisToolsWidget.CurrentTab == 1;
        if (ImGui::MenuItem("Network Analyser", NULL, net_on))
        {
            if (net_on)
                analysisToolsWidget.ToolsOn = false;
            else
            {
                analysisToolsWidget.ToolsOn = true;
                analysisToolsWidget.CurrentTab = 1;
                analysisToolsWidget.RequestedTab = 1;
                showPanel(PanelAnalysis);
            }
        }
        ImGui::Separator();

        const bool mm = InputsWidget.mode() == InputsControl::Mode::Multimeter;
        if (ImGui::MenuItem("Multimeter", NULL, mm))
        {
            if (mm)
                InputsWidget.setMode(InputsControl::Mode::ScopeScope);
            else
            {
                InputsWidget.setMode(InputsControl::Mode::Multimeter);
                showPanel(PanelMeter);
            }
        }
        const bool logic
            = InputsWidget.channelIsLogic(1) || InputsWidget.channelIsLogic(2);
        if (ImGui::MenuItem("Logic Analyzer", NULL, logic))
        {
            if (logic)
                InputsWidget.setMode(InputsControl::Mode::ScopeScope);
            else
            {
                InputsWidget.setMode(InputsControl::Mode::ScopeLogic);
                showPanel(PanelLogic);
            }
        }
        ImGui::Separator();

        if (ImGui::MenuItem("Data Logger..."))
            showPanel(PanelRecord);
        if (ImGui::MenuItem("Replay DAQ file..."))
        {
            showPanel(PanelRecord);
            DAQReplayWidget.openFilePrompt();
        }
        ImGui::EndMenu();
    }

    // --- View ---
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Side Panel", "B", m_sidebar_visible))
            m_sidebar_visible = !m_sidebar_visible;
        if (ImGui::BeginMenu("Side Panel Page"))
        {
            for (int p = 0; p < PanelCount; p++)
            {
                if (ImGui::MenuItem(panelName(p), NULL,
                        m_sidebar_visible && m_panel == p))
                    showPanel(p);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Layout"))
        {
            auto layoutItem = [&app](const char* label, App::LayoutMode mode) {
                if (ImGui::MenuItem(label, NULL, app.layoutMode() == mode))
                    app.setLayoutMode(mode);
            };
            layoutItem("Auto", App::LayoutMode::Auto);
            layoutItem("Desktop", App::LayoutMode::Desktop);
            layoutItem("Mobile", App::LayoutMode::Mobile);
            layoutItem("Compact (800x480)", App::LayoutMode::Compact);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme"))
        {
            if (ImGui::MenuItem("Dark", NULL, app.darkTheme()))
                app.setDarkTheme(true);
            if (ImGui::MenuItem("Light", NULL, !app.darkTheme()))
                app.setDarkTheme(false);
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("Demo windows", NULL, &app.showDemoWindows());
            ImGui::MenuItem("Debug console", NULL, &app.showDebugConsole());
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- Help ---
    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem("User Guide", NULL, &showHelpWindow);
        ImGui::MenuItem("Keyboard Shortcuts", "F1", &app.showShortcuts());
        ImGui::Separator();
        if (ImGui::MenuItem("Pinout Diagram"))
            GeneralHelp.show_help = true;
        if (ImGui::MenuItem("Troubleshooting"))
            TroubleShoot.show_help = true;
        ImGui::Separator();
        if (ImGui::MenuItem("About EspoTek Labrador"))
            m_show_about = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ---- Toolbar ----------------------------------------------------------------

void DesktopFrontend::renderToolbar(App& app, float height)
{
    (void)app;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::BeginChild("##toolbar", ImVec2(0, height),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const float btn_h = ImGui::GetFrameHeight();

    // Run/Stop — shows the current acquisition state, click (or Space) toggles
    const bool running = !OSCWidget.Paused;
    if (SolidButton(running ? "Running##runstop" : "Stopped##runstop",
            running ? ImVec4(0.07f, 0.53f, 0.0f, 1.0f) : ImVec4(0.56f, 0.0f, 0.0f, 1.0f),
            ImVec2(92, btn_h)))
        OSCWidget.Paused = !OSCWidget.Paused;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run / stop acquisition (Space)");

    ImGui::SameLine();
    if (WhiteOutlineButton("Auto Fit##toolbar", ImVec2(90, btn_h)))
    {
        OSCWidget.AutofitX = true;
        OSCWidget.AutofitY = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fit both axes to the signal (F)");

    ToolbarSep();

    // Input mode — owns the device mode, replaces the old Inputs widget
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Inputs");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::BeginCombo(
            "##toolbar_mode", InputsControl::modeLabel(InputsWidget.selectedIndex())))
    {
        for (int i = 0; i < InputsControl::ModeCount; i++)
        {
            const bool sel = (i == InputsWidget.selectedIndex());
            if (ImGui::Selectable(InputsControl::modeLabel(i), sel))
                InputsWidget.selectIndex(i);
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ToolbarSep();

    // Hardware gain (also in Scope > Hardware Gain; combo here for reach)
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Gain");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    char cur_gain[16];
    snprintf(cur_gain, sizeof cur_gain, "%gx", OSCWidget.getSelectedGain());
    if (ImGui::BeginCombo("##toolbar_gain", cur_gain))
    {
        for (int i = 0; i < OSCControl::GainValueCount; i++)
        {
            char lab[16];
            snprintf(lab, sizeof lab, "%gx", OSCControl::GainValues[i]);
            if (ImGui::Selectable(lab, OSCWidget.GainComboCurrentItem == i))
            {
                OSCWidget.GainComboCurrentItem = i;
                OSCWidget.AutoGain = false; // manual pick overrides auto (Qt rule)
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto##toolbar_gain_auto", &OSCWidget.AutoGain);

    ToolbarSep();

    ToolToggle("Cursor 1##toolbar", &OSCWidget.Cursor1toggle);
    ImGui::SameLine();
    ToolToggle("Cursor 2##toolbar", &OSCWidget.Cursor2toggle);

    // Right edge: side panel toggle
    const float panel_btn_w = 100.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - panel_btn_w - 8.0f);
    if (ImGui::Button(m_sidebar_visible ? "Hide Panel##tb" : "Show Panel##tb",
            ImVec2(panel_btn_w, btn_h)))
        m_sidebar_visible = !m_sidebar_visible;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show / hide the side panel (B)");

    ImGui::EndChild();
}

// ---- Rail -------------------------------------------------------------------

void DesktopFrontend::renderRail(float height)
{
    const float rail_w = ImGui::GetFontSize() * 5.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 6));
    ImGui::BeginChild("##rail", ImVec2(rail_w, height),
        ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    const float btn_h = ImGui::GetFontSize() * 2.4f;
    for (int p = 0; p < PanelCount; p++)
    {
        const bool selected = m_sidebar_visible && (m_panel == p);
        const float* ac = panelAccent(p);
        const ImVec4 accent(ac[0], ac[1], ac[2], 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button,
            selected ? ImVec4(accent.x, accent.y, accent.z, 0.30f)
                     : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.45f));
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 0.60f));
        if (ImGui::Button(panelName(p), ImVec2(-FLT_MIN, btn_h)))
        {
            if (selected)
                m_sidebar_visible = false; // click the active page to collapse
            else
                showPanel(p);
        }
        ImGui::PopStyleColor(3);

        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        if (selected)
            ImGui::GetWindowDrawList()->AddRectFilled(mn,
                ImVec2(mn.x + 3.0f, mx.y), ImGui::ColorConvertFloat4ToU32(accent));

        // Red dot on the Record page while the DAQ writer is running
        if (p == PanelRecord && DAQWidget.isRecording())
        {
            const float r = 4.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(mx.x - r - 4.0f, mn.y + r + 4.0f), r, IM_COL32(230, 40, 40, 255));
        }
    }

    ImGui::EndChild();
}

// ---- Side panel -------------------------------------------------------------

void DesktopFrontend::renderSidePanel(App& app, float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::BeginChild("##sidepanel", ImVec2(m_sidebar_width, height),
        ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    static const char* titles[PanelCount] = { "Oscilloscope", "Signal Outputs",
        "Multimeter", "Logic Analyzer", "Data Logger", "Analysis Tools" };
    ControlWidget* help_for[PanelCount] = { &OSCWidget, &SG1Widget,
        &MultimeterWidget, &LogicWidget, &DAQWidget, &analysisToolsWidget };

    // Title bar: accent-tinted strip + page name + help
    const float* ac = panelAccent(m_panel);
    const float title_h = ImGui::GetFrameHeight() + 8.0f;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 tl = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(tl, ImVec2(tl.x + w, tl.y + title_h),
        ImGui::ColorConvertFloat4ToU32(ImVec4(ac[0], ac[1], ac[2], 0.25f)), 5.0f);
    dl->AddRectFilled(tl, ImVec2(tl.x + 4.0f, tl.y + title_h),
        ImGui::ColorConvertFloat4ToU32(ImVec4(ac[0], ac[1], ac[2], 1.0f)), 5.0f,
        ImDrawFlags_RoundCornersLeft);

    ImGui::SetCursorScreenPos(
        ImVec2(tl.x + 12.0f, tl.y + (title_h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted(titles[m_panel]);

    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(tl.x + w - 28.0f,
        tl.y + (title_h - ImGui::GetFrameHeight()) * 0.5f + 2.0f));
    if (ImGui::SmallButton("?##panel_help"))
        help_for[m_panel]->show_help = true;

    ImGui::SetCursorScreenPos(ImVec2(tl.x, tl.y + title_h + 8.0f));

    // Body scrolls per page (child ID per page keeps scroll positions apart)
    ImGui::BeginChild(panelName(m_panel), ImVec2(0, 0));
    switch (m_panel)
    {
    case PanelScope: renderScopePanel(); break;
    case PanelSignals: renderSignalsPanel(); break;
    case PanelMeter: renderMeterPanel(); break;
    case PanelLogic: renderLogicPanel(); break;
    case PanelRecord: renderRecordPanel(app); break;
    case PanelAnalysis: renderAnalysisPanel(); break;
    default: break;
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

// ---- Panel bodies -----------------------------------------------------------

void DesktopFrontend::renderScopePanel()
{
    ImGui::SeparatorText("Display");
    OSCWidget.renderDisplaySection(false); // XY/eye live in the Scope menu

    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::SeparatorText("Trigger");
    OSCWidget.renderTriggerSection(false); // gain lives in the toolbar

    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::SeparatorText("Probes");
    OSCWidget.renderProbeSection();

    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::SeparatorText("Math Channel");
    OSCWidget.renderMathSection();
}

void DesktopFrontend::renderSignalsPanel()
{
    // Each section gets its own ID scope: unlike the old TreeNode chrome,
    // CollapsingHeader doesn't push one, and SG1/SG2 reuse identical child
    // names internally ("Sine_control" etc.).
    if (SectionHeader("Power Supply", constants::PSU_ACCENT))
    {
        ImGui::PushID("psu");
        PSUWidget.renderControl();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6.0f));

    if (SectionHeader("Signal Generator 1", constants::SG1_ACCENT))
    {
        ImGui::PushID("sg1");
        SG1Widget.renderControl();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6.0f));

    if (SectionHeader("Signal Generator 2", constants::SG2_ACCENT))
    {
        ImGui::PushID("sg2");
        SG2Widget.renderControl();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6.0f));

    if (SectionHeader("Digital Outputs", constants::GEN_ACCENT))
    {
        ImGui::PushID("dout");
        DigitalOutWidget.renderControl();
        ImGui::PopID();
    }
}

void DesktopFrontend::renderMeterPanel()
{
    if (InputsWidget.mode() != InputsControl::Mode::Multimeter)
    {
        ImGui::TextWrapped("The multimeter measures across the DUT+ / DUT- pins "
                           "and needs the board in multimeter mode.");
        ImGui::Spacing();
        if (WhiteOutlineButton("Switch to multimeter mode", ImVec2(240, 30)))
            InputsWidget.setMode(InputsControl::Mode::Multimeter);
        return;
    }

    MultimeterWidget.renderControl();

    ImGui::Dummy(ImVec2(0, 10.0f));
    if (WhiteOutlineButton("Leave multimeter mode", ImVec2(200, 26)))
        InputsWidget.setMode(InputsControl::Mode::ScopeScope);
}

void DesktopFrontend::renderLogicPanel()
{
    const bool ch1 = InputsWidget.channelIsLogic(1);
    const bool ch2 = InputsWidget.channelIsLogic(2);
    if (!ch1 && !ch2)
    {
        ImGui::TextWrapped("Logic decoding needs an input channel in logic mode.");
        ImGui::Spacing();
        if (WhiteOutlineButton("CH1 scope + CH2 logic", ImVec2(240, 30)))
            InputsWidget.setMode(InputsControl::Mode::ScopeLogic);
        if (WhiteOutlineButton("CH1 + CH2 logic (I2C)", ImVec2(240, 30)))
            InputsWidget.setMode(InputsControl::Mode::LogicLogic);
        return;
    }

    LogicWidget.renderControl();
}

void DesktopFrontend::renderRecordPanel(App& app)
{
    (void)app;
    if (SectionHeader("Record to File", constants::NETWORK_ANALYSER_ACCENT))
    {
        ImGui::PushID("daq");
        DAQWidget.renderControl();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6.0f));

    if (SectionHeader("Replay a Recording", constants::GEN_ACCENT))
    {
        ImGui::PushID("replay");
        DAQReplayWidget.renderControl();
        ImGui::PopID();
    }
}

void DesktopFrontend::renderAnalysisPanel()
{
    analysisToolsWidget.renderControl();
}

// ---- Status bar -------------------------------------------------------------

void DesktopFrontend::renderStatusBar(App& app)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 5));
    ImGui::BeginChild("##statusbar", ImVec2(0, 0),
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const ImVec4 green(0.1f, 0.85f, 0.1f, 1.0f);
    const ImVec4 red(0.95f, 0.2f, 0.2f, 1.0f);
    const ImVec4 orange(1.0f, 0.6f, 0.0f, 1.0f);

    char left_buf[128];
    const char* left = left_buf;
    ImVec4 dot = red;
    bool warn = false;
    if (app.isFlashing())
    {
        dot = orange;
        warn = true;
        left = app.isRecovering()
            ? "Recovering board from bootloader mode — do not unplug"
            : "Flashing firmware — do not unplug";
    }
    else if (app.isConnected() && app.safetyMode())
    {
        dot = red;
        warn = true;
        left = "Safety mode — disconnect and reconnect the board";
    }
    else if (app.isConnected() && app.uninitialisedMode())
    {
        dot = red;
        warn = true;
        left = "Uninitialised state — disconnect and reconnect the board";
    }
    else if (app.isConnected())
    {
        dot = green;
        snprintf(left_buf, sizeof left_buf, "Connected — firmware %hu.%hhu",
            librador_get_device_firmware_version(),
            librador_get_device_firmware_variant());
    }
    else if (app.bootloaderSeen())
    {
        dot = orange;
        warn = true;
        left = "Board in bootloader mode";
    }
    else
    {
        dot = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        left = "No Labrador found — plug in a board";
    }

    // Status dot + text
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ImGui::GetTextLineHeight() * 0.30f;
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.55f), r,
        ImGui::ColorConvertFloat4ToU32(dot));
    ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, 0));
    ImGui::SameLine();
    if (warn)
        ImGui::TextColored(dot, "%s", left);
    else
        ImGui::TextUnformatted(left);

    // Right cluster: input mode | sample rate | gain (+ REC while recording)
    char right[192];
    const char* rate = (InputsWidget.mode() == InputsControl::Mode::Scope750)
        ? "750 kSa/s"
        : "375 kSa/s";
    snprintf(right, sizeof right, "%s   |   %s   |   Gain %gx%s",
        InputsControl::modeLabel(InputsWidget.selectedIndex()), rate,
        OSCWidget.getSelectedGain(), OSCWidget.AutoGain ? " (auto)" : "");
    const float rw = ImGui::CalcTextSize(right).x;
    const bool rec = DAQWidget.isRecording();
    const float rec_w = rec ? ImGui::CalcTextSize("REC").x + 18.0f : 0.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - rw - rec_w - 12.0f);
    if (rec)
    {
        ImGui::TextColored(red, "REC");
        ImGui::SameLine(0.0f, 10.0f);
    }
    ImGui::TextColored(constants::GRAY_TEXT, "%s", right);

    ImGui::EndChild();
}

// ---- Floating windows -------------------------------------------------------

void DesktopFrontend::renderCalibrationWindow()
{
    if (!m_show_calibration && !CalibrationWidget.wizardActive())
        return;

    // Fixed width so TextWrapped instructions lay out sanely; height follows.
    ImGui::SetNextWindowSizeConstraints(ImVec2(460, 0), ImVec2(460, FLT_MAX));
    bool open = true;
    if (ImGui::Begin("Calibration", &open, ImGuiWindowFlags_AlwaysAutoResize))
        CalibrationWidget.renderControl();
    ImGui::End();

    if (!open)
        m_show_calibration = false;
    else if (CalibrationWidget.wizardActive())
        m_show_calibration = true; // wizard keeps the window until it finishes
}

void DesktopFrontend::renderAboutWindow()
{
    if (!m_show_about)
        return;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::Begin("About EspoTek Labrador", &m_show_about,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::Text("EspoTek Labrador");
        ImGui::TextColored(constants::GRAY_TEXT, "Unified interface 2.0");
        ImGui::Separator();
        ImGui::Text("Oscilloscope, signal generators, power supply,\n"
                    "logic analyzer and multimeter on one board.");
        ImGui::TextColored(constants::GRAY_TEXT, "Firmware target %hu.%hhu",
            constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
        ImGui::Spacing();
        if (ImGui::Button("espotek.com"))
            SDL_OpenURL("https://espotek.com");
        ImGui::SameLine();
        if (ImGui::Button("Source on GitHub"))
            SDL_OpenURL("https://github.com/espotek-org/Labrador");
    }
    ImGui::End();
}

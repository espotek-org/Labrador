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
#include <cmath>
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
    QaMarkItemChecked(*v);
    if (on)
        ImGui::PopStyleColor();
}

static void ToolbarSep()
{
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, 10.0f);
}

// ---- CRT chrome (retro themes) ----------------------------------------------

// Frame the layout segment just closed (call right after EndChild). The CRT
// themes use the bright phosphor line; the classic themes a subtle gray.
static void FrameLastSegment()
{
    const ThemeSpec& t = CurrentTheme();
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec4 col = t.retro ? ImVec4(t.line.x, t.line.y, t.line.z, 0.75f) : t.lineDim;
    const float rounding = t.retro ? 0.0f : ImGui::GetStyle().ChildRounding;
    ImGui::GetWindowDrawList()->AddRect(
        mn, mx, ImGui::ColorConvertFloat4ToU32(col), rounding, 0, 1.0f);
}

// Vector-drawn CRT bezel around the scope area: soft glow, double frame,
// corner brackets and graticule ticks at the edge midpoints.
static void DrawCrtBezel(const ImVec2& mn, const ImVec2& mx)
{
    const ThemeSpec& t = CurrentTheme();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line = ImGui::ColorConvertFloat4ToU32(t.line);
    const ImU32 dim = ImGui::ColorConvertFloat4ToU32(t.lineDim);
    auto glow = [&](float a) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(t.line.x, t.line.y, t.line.z, a));
    };
    const float sc = ImGui::GetStyle().FontScaleDpi > 0 ? ImGui::GetStyle().FontScaleDpi : 1.0f;

    // Phosphor glow bleeding outward
    dl->AddRect(ImVec2(mn.x - 2, mn.y - 2), ImVec2(mx.x + 2, mx.y + 2), glow(0.20f), 2.0f);
    dl->AddRect(ImVec2(mn.x - 4, mn.y - 4), ImVec2(mx.x + 4, mx.y + 4), glow(0.08f), 4.0f);
    // Main frame + inset line
    dl->AddRect(mn, mx, line, 0.0f, 0, 1.5f);
    dl->AddRect(ImVec2(mn.x + 4, mn.y + 4), ImVec2(mx.x - 4, mx.y - 4), dim, 0.0f, 0, 1.0f);

    // Corner brackets (viewfinder style), drawn over the main frame
    const float b = 14.0f * sc;
    const float th = 2.0f * sc;
    // top-left
    dl->AddLine(ImVec2(mn.x, mn.y + th * 0.5f), ImVec2(mn.x + b, mn.y + th * 0.5f), line, th);
    dl->AddLine(ImVec2(mn.x + th * 0.5f, mn.y), ImVec2(mn.x + th * 0.5f, mn.y + b), line, th);
    // top-right
    dl->AddLine(ImVec2(mx.x - b, mn.y + th * 0.5f), ImVec2(mx.x, mn.y + th * 0.5f), line, th);
    dl->AddLine(ImVec2(mx.x - th * 0.5f, mn.y), ImVec2(mx.x - th * 0.5f, mn.y + b), line, th);
    // bottom-left
    dl->AddLine(ImVec2(mn.x, mx.y - th * 0.5f), ImVec2(mn.x + b, mx.y - th * 0.5f), line, th);
    dl->AddLine(ImVec2(mn.x + th * 0.5f, mx.y - b), ImVec2(mn.x + th * 0.5f, mx.y), line, th);
    // bottom-right
    dl->AddLine(ImVec2(mx.x - b, mx.y - th * 0.5f), ImVec2(mx.x, mx.y - th * 0.5f), line, th);
    dl->AddLine(ImVec2(mx.x - th * 0.5f, mx.y - b), ImVec2(mx.x - th * 0.5f, mx.y), line, th);

    // Graticule ticks at the midpoint of each edge
    const float tick = 5.0f * sc;
    const float cx = (mn.x + mx.x) * 0.5f;
    const float cy = (mn.y + mx.y) * 0.5f;
    dl->AddLine(ImVec2(cx, mn.y), ImVec2(cx, mn.y + tick), line, 1.5f);
    dl->AddLine(ImVec2(cx, mx.y - tick), ImVec2(cx, mx.y), line, 1.5f);
    dl->AddLine(ImVec2(mn.x, cy), ImVec2(mn.x + tick, cy), line, 1.5f);
    dl->AddLine(ImVec2(mx.x - tick, cy), ImVec2(mx.x, cy), line, 1.5f);
}

// Horizontal scanlines over the CRT face (drawn into the current window's
// draw list, so call from inside the plot child after the plot renders).
static void DrawScanlines(const ImVec2& mn, const ImVec2& mx)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float sc = ImGui::GetStyle().FontScaleDpi > 0 ? ImGui::GetStyle().FontScaleDpi : 1.0f;
    const float step = 3.0f * sc;
    const ImU32 shade = IM_COL32(0, 0, 0, 44);
    for (float y = mn.y + step; y < mx.y; y += step)
        dl->AddRectFilled(ImVec2(mn.x, y), ImVec2(mx.x, y + 1.0f * sc), shade);
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
        = { "Scope", "Signals", "PSU", "Meter", "Logic", "DAQ", "Analysis" };
    return names[p];
}

void DesktopFrontend::startUp(App& app)
{
    InstrumentFrontend::startUp(app);
    // The desktop layout hosts UART TX on the Logic page (renderLogicPanel)
    // instead of inline under SG1's waveform controls, and the plot-help "?"
    // in the toolbar instead of under the plot.
    SG1Widget.ShowUartInline = false;
    PlotWidgetObj.ShowHelpButton = false;
}

const float* DesktopFrontend::panelAccent(int p)
{
    // Most slots point into the theme-written accent arrays, so the rail and
    // page chrome re-colour with the theme (mono CRT themes collapse them all
    // to the phosphor line colour, Arcade installs its neon palette).
    const ThemeSpec& t = CurrentTheme();
    switch (p)
    {
    // Scope: classic themes keep the CH1-yellow rail; CRT themes use the
    // themed scope chrome slot.
    case PanelScope: return t.retro ? constants::OSC_ACCENT : constants::OSC1_ACCENT;
    case PanelSignals: return constants::SG1_ACCENT;
    case PanelPSU: return constants::PSU_ACCENT;
    // Meter: MATH is a trace colour, so mono themes swap in a themed slot
    // (they are all the line colour anyway).
    case PanelMeter:
        return (t.retro && t.mono) ? constants::OSC_ACCENT : constants::MATH_ACCENT;
    case PanelLogic: return constants::SPECTRUM_ANALYSER_ACCENT;
    case PanelDAQ: return constants::NETWORK_ANALYSER_ACCENT;
    default: return constants::SG2_ACCENT; // Analysis
    }
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
    m_scanlines = s.getBool("desk_scanlines", true);
}

void DesktopFrontend::saveSettings(Settings& s)
{
    InstrumentFrontend::saveSettings(s);
    s.set("desk_panel_visible", m_sidebar_visible);
    s.set("desk_panel_page", (double)m_panel);
    s.set("desk_panel_width", (double)m_sidebar_width);
    s.set("desk_scanlines", m_scanlines);
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
    FrameLastSegment();

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

    const bool retro = CurrentTheme().retro;
    // CRT themes reserve extra padding for the bezel's inset line
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        retro ? ImVec2(10, 10) : ImVec2(6, 6));
    ImGui::BeginChild("##plotarea", ImVec2(plot_w, middle_h),
        ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    PlotWidgetObj.setSize(ImGui::GetContentRegionAvail());
    PlotWidgetObj.Render();
    if (retro && m_scanlines)
    {
        // Scanlines live in the child's draw list so they overlay the plot
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        DrawScanlines(wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    }
    ImGui::EndChild();
    if (retro)
        DrawCrtBezel(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    else
        FrameLastSegment(); // classic themes frame the plot area too

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
        FrameLastSegment();
    }

    ImGui::SameLine(0.0f, 0.0f);
    renderRail(middle_h);
    FrameLastSegment();

    renderStatusBar(app);
    FrameLastSegment();

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
            showPanel(PanelDAQ);
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

        if (ImGui::MenuItem("DAQ Recorder..."))
            showPanel(PanelDAQ);
        if (ImGui::MenuItem("Replay DAQ file..."))
        {
            showPanel(PanelDAQ);
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
            for (int i = 0; i < ThemeCount(); i++)
            {
                const ThemeSpec& t = ThemeAt(i);
                if (ImGui::MenuItem(t.label, NULL, app.themeId() == t.id))
                    app.setThemeId(t.id);
            }
            ImGui::Separator();
            ImGui::BeginDisabled(!CurrentTheme().retro);
            ImGui::MenuItem("CRT Scanlines", NULL, &m_scanlines);
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Text Size"))
        {
            auto sizeItem = [&app](const char* label, float s) {
                if (ImGui::MenuItem(label, NULL, std::fabs(app.fontScale() - s) < 0.01f))
                    app.setFontScale(s);
            };
            sizeItem("Small", 0.85f);
            sizeItem("Normal", 1.0f);
            sizeItem("Large", 1.2f);
            sizeItem("Extra Large", 1.45f);
            ImGui::EndMenu();
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Debug"))
        {
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

    // Button widths follow the font (the picker includes some very wide
    // faces that would overflow fixed pixel widths).
    const float run_w = ImGui::CalcTextSize("RUNNING").x + 24.0f;

    // Run/Stop — shows the current acquisition state, click (or Space)
    // toggles. CRT themes use inverse video for the running state (mono
    // terminals had no red/green); classic themes keep the coloured button.
    const bool running = !OSCWidget.Paused;
    const ThemeSpec& th = CurrentTheme();
    bool run_clicked;
    if (th.retro)
    {
        auto L = [&th](float a) { return ImVec4(th.line.x, th.line.y, th.line.z, a); };
        ImGui::PushStyleColor(ImGuiCol_Button, running ? L(0.85f) : L(0.06f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, running ? L(1.0f) : L(0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, running ? L(0.70f) : L(0.30f));
        ImGui::PushStyleColor(ImGuiCol_Text, running ? th.bg : th.dim);
        run_clicked = ImGui::Button(
            running ? "RUNNING###runstop" : "STOPPED###runstop", ImVec2(run_w, btn_h));
        ImGui::PopStyleColor(4);
    }
    else
    {
        run_clicked = SolidButton(running ? "Running###runstop" : "Stopped###runstop",
            running ? ImVec4(0.07f, 0.53f, 0.0f, 1.0f) : ImVec4(0.56f, 0.0f, 0.0f, 1.0f),
            ImVec2(run_w, btn_h));
    }
    QaMarkItemChecked(running);
    if (run_clicked)
        OSCWidget.Paused = !OSCWidget.Paused;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Run / stop acquisition (Space)");

    ImGui::SameLine();
    if (WhiteOutlineButton("Auto Fit##toolbar",
            ImVec2(ImGui::CalcTextSize("Auto Fit").x + 24.0f, btn_h)))
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

    // Hardware gain lives in the Scope > Hardware Gain menu, which is the
    // single source of truth (it was duplicated here next to the cursors).

    ToolToggle("Cursor 1##toolbar", &OSCWidget.Cursor1toggle);
    ImGui::SameLine();
    ToolToggle("Cursor 2##toolbar", &OSCWidget.Cursor2toggle);

    // Plot help (relocated from under the plot)
    ImGui::SameLine();
    if (ImGui::Button(" ? ##plot_help", ImVec2(0, btn_h)))
        PlotWidgetObj.show_help = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plot window help");

    // Right edge: side panel toggle
    const float panel_btn_w = ImGui::CalcTextSize("Show Panel").x + 22.0f;
    ImGui::SameLine(std::max(ImGui::GetWindowWidth() - panel_btn_w - 8.0f,
        ImGui::GetCursorPosX() + 10.0f));
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
    ImGui::PushID("rail"); // qualifies the page names for the QA item paths
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
        if (p == PanelDAQ && DAQWidget.isRecording())
        {
            const float r = 4.0f;
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(mx.x - r - 4.0f, mn.y + r + 4.0f), r, IM_COL32(230, 40, 40, 255));
        }
    }
    ImGui::PopID();

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
        "Power Supply", "Multimeter", "Logic Analyzer", "DAQ", "Analysis Tools" };
    ControlWidget* help_for[PanelCount] = { &OSCWidget, &SG1Widget, &PSUWidget,
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
    (void)app;
    switch (m_panel)
    {
    case PanelScope: renderScopePanel(); break;
    case PanelSignals: renderSignalsPanel(); break;
    case PanelPSU: renderPSUPanel(); break;
    case PanelMeter: renderMeterPanel(); break;
    case PanelLogic: renderLogicPanel(); break;
    case PanelDAQ: renderDAQPanel(); break;
    case PanelAnalysis: renderAnalysisPanel(); break;
    default: break;
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

// ---- Panel bodies -----------------------------------------------------------

void DesktopFrontend::renderScopePanel()
{
    // Acquisition run/pause, mirroring the toolbar button and Space
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Acquisition");
    ImGui::SameLine();
    bool running = !OSCWidget.Paused;
    if (ToggleSwitch("##scope_panel_run", &running,
            colourConvert(constants::GEN_ACCENT)))
        OSCWidget.Paused = !running;
    ImGui::SameLine();
    if (running)
        ImGui::TextUnformatted("Running");
    else
        ImGui::TextColored(constants::GRAY_TEXT, "Paused");

    ImGui::Dummy(ImVec2(0, 8.0f));
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

void DesktopFrontend::renderPSUPanel()
{
    PSUWidget.renderControl();
    ImGui::Spacing();
    ImGui::TextColored(constants::GRAY_TEXT,
        "4.5 - 11 V programmable supply on the PSU pin.");
    ImGui::TextColored(constants::GRAY_TEXT,
        "Also limits the signal generators' output range.");
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
    }
    else
    {
        LogicWidget.renderControl();
    }

    // UART transmit lives with the rest of the serial tooling (state and
    // servicing stay in SG1, which owns the CH1 output).
    ImGui::Dummy(ImVec2(0, 10.0f));
    if (SectionHeader("UART TX", constants::SG1_ACCENT))
    {
        ImGui::PushID("uart_tx");
        SG1Widget.renderUartControl();
        ImGui::PopID();
    }
}

void DesktopFrontend::renderDAQPanel()
{
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
    // Never overlap the left status text (very wide fonts / narrow windows)
    ImGui::SameLine(std::max(ImGui::GetWindowWidth() - rw - rec_w - 12.0f,
        ImGui::GetCursorPosX() + 16.0f));
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

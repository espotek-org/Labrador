#include "ui/InstrumentFrontend.h"

#include "app/App.h"
#include "app/settings.h"
#include "app/textures.h"
#include "platform/paths.h"
#include "librador.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Vestigial global pinout handles (declared extern in util.h). The pinout
// images are actually stored per-widget via setPinoutImg below; these globals
// are unused but kept defined for the extern declarations. They live here with
// the pinout-loading code that used to sit in App::StartUp.
int constants::pinout_width;
int constants::pinout_height;
int constants::glob_pinout_width;
int constants::glob_pinout_height;
intptr_t constants::psu_pinout_texture = 0;
intptr_t constants::sg_pinout_texture;
intptr_t constants::osc_pinout_texture;
intptr_t constants::glob_pinout_texture;

// ---- Hardware scope gain ---------------------------------------------------
// Frontend maths from the Qt app (Desktop_Interface/xmega.h): vcc = 3.3 V,
// vref = vcc/2, input divider R3 = 1 MOhm / R4 = 75 kOhm.  Full scale at ADC
// gain g spans vref +/- (vcc/2)*(R3+R4)/R4 / g = 1.65 +/- 23.65/g volts.  The
// merged librador converts samples with the same constants (o1buffer.cpp
// sampleConvert: frontendGain = 75/1075, voltage_ref = 1.65), so the converted
// voltages returned by librador_get_analog_data_by_rate can be compared
// directly against this range.
static constexpr double SCOPE_VREF = 1.65;
static constexpr double SCOPE_HALF_RANGE_UNITY = 23.65; // volts either side of vref at gain 1

static double scopeHalfRange(double gain) { return SCOPE_HALF_RANGE_UNITY / gain; }

// View-driven auto-gain (Qt behaviour, per Chris 2026-07-04): each frame,
// derive the gain that fits the plot's visible y-range with Qt's 2% headroom
// (isodriver.cpp autoGain, lines 552-565), compare against the last gain
// sent, and issue the USB command only when the value actually changes (the
// change guard in applyScopeGain protects against the USB sampling bug).
static constexpr double AUTOGAIN_HEADROOM = 0.98; // Qt's 0.98 (isodriver.cpp:555)

static double s_applied_gain = 0.0; // last gain sent to the device; 0 = none yet

// Send a gain to the hardware.  Hardware quirk: setting the gain too often
// causes a USB sampling bug, so this only touches the device when the value
// actually changes (the Qt driver has the same guard —
// genericusbdriver.cpp:355 "No update!").
static void applyScopeGain(double gain)
{
    if (gain == s_applied_gain)
        return;
    if (librador_set_oscilloscope_gain(gain) >= 0)
        s_applied_gain = gain;
}

static void updateAutoGain(OSCControl& osc, const InputsControl& inputs, const PlotWidget& plot)
{
    if (!librador_is_connected() || !librador_iso_thread_is_active())
        return;
    if (!plot.VisibleYValid)
        return; // XY/spectrum/network view showing - keep the current gain
    if (s_applied_gain <= 0.0)
        return; // nothing applied yet (not connected)

    // The axes show displayed volts; undo each scope channel's display
    // transform (displayed = measured/attenuation + offset) to get the
    // measured swing the hardware range must cover.
    double wanted = 0.0;
    bool any_channel = false;
    for (int ch = 1; ch <= 2; ch++)
    {
        if (!inputs.channelIsScope(ch))
            continue;
        const double atten = (ch == 1) ? osc.getAttenuationCH1() : osc.getAttenuationCH2();
        const double offset = (ch == 1) ? osc.OffsetCH1 : osc.OffsetCH2;
        const double devs[2] = { plot.VisibleYMin, plot.VisibleYMax };
        for (double y : devs)
            wanted = std::max(wanted, std::fabs((y - offset) * atten - SCOPE_VREF));
        any_channel = true;
    }
    if (!any_channel || wanted <= 0.0)
        return;

    // Qt's snap: the largest gain whose full-scale range still covers the
    // wanted range with 2% headroom; fall back to the widest range (0.5x).
    double target = OSCControl::GainValues[0];
    for (int i = OSCControl::GainValueCount - 1; i >= 0; i--)
    {
        if (AUTOGAIN_HEADROOM * scopeHalfRange(OSCControl::GainValues[i]) >= wanted)
        {
            target = OSCControl::GainValues[i];
            break;
        }
    }

    if (target != s_applied_gain)
    {
        applyScopeGain(target); // change-guarded: one USB command per change
        osc.setSelectedGain(target);
    }
}

void InstrumentFrontend::startUp(App& app)
{
    (void)app;

    // Load pinout images (from memory — works from the Android APK too)
    unsigned int psu_tmp_texture = 0, sg_tmp_texture = 0, osc_tmp_texture = 0,
                 glob_tmp_texture = 0;
    int w, h, gw, gh;
    std::vector<unsigned char> psu_png = loadAsset("media/psu-pinout.png");
    std::vector<unsigned char> sg_png = loadAsset("media/sg-pinout.png");
    std::vector<unsigned char> osc_png = loadAsset("media/osc-pinout.png");
    std::vector<unsigned char> glob_png = loadAsset("media/global-pinout.png");
    bool psu_ret
        = LoadTextureFromMemory(psu_png.data(), psu_png.size(), &psu_tmp_texture, &w, &h);
    bool sg_ret
        = LoadTextureFromMemory(sg_png.data(), sg_png.size(), &sg_tmp_texture, &w, &h);
    bool osc_ret
        = LoadTextureFromMemory(osc_png.data(), osc_png.size(), &osc_tmp_texture, &w, &h);
    bool glob_ret = LoadTextureFromMemory(
        glob_png.data(), glob_png.size(), &glob_tmp_texture, &gw, &gh);
    if (!psu_ret || !sg_ret || !osc_ret || !glob_ret)
        throw std::runtime_error("Failed to load pinout textures");

    PSUWidget.setPinoutImg((intptr_t)psu_tmp_texture, w, h);
    SG1Widget.setPinoutImg((intptr_t)sg_tmp_texture, w, h);
    SG2Widget.setPinoutImg((intptr_t)sg_tmp_texture, w, h);
    OSCWidget.setPinoutImg((intptr_t)osc_tmp_texture, w, h);
    GeneralHelp.setPinoutImg((intptr_t)glob_tmp_texture, gw, gh);

    // Set pointers in widgets to objects owned by this frontend
    PlotWidgetObj.SetNetworkAnalyser(&na, &na_cfg);
    analysisToolsWidget.SetNetworkAnalyser(&na, &na_cfg);
    PlotWidgetObj.SetControllers(&OSCWidget, &analysisToolsWidget);

    PlotWidgetObj.SetOscs(&OSC1Data, &OSC2Data, &MathData);
    OSCWidget.SetOscs(&OSC1Data, &OSC2Data, &MathData);

    PlotWidgetObj.SetPlots(&spectrum_plots, &network_plots);
    analysisToolsWidget.SetPlots(&spectrum_plots, &network_plots);

    MultimeterWidget.SetInputs(&InputsWidget);
    LogicWidget.SetInputs(&InputsWidget);
    DAQWidget.SetInputs(&InputsWidget);

    init_constants();
    loadHelpFile();
}

void InstrumentFrontend::update(App& app)
{
    handleShortcuts(app);
    renderLayout(app);

    if (showHelpWindow)
        renderHelpWindow(&showHelpWindow);
    for (ControlWidget* w : widgets)
    {
        if (w->show_help)
            w->renderHelp();
    }
}

void InstrumentFrontend::shutDown(App& app)
{
    (void)app;
    // Turn off signal generators
    SG1Widget.reset();
    SG2Widget.reset();
}

void InstrumentFrontend::onDeviceConnected(App& app)
{
    (void)app;
    // Same post-connect init the Monash app performed
    SG1Widget.reset();
    SG2Widget.reset();
    // Device mode is owned by the Inputs widget; force a resend of the
    // selected mode and the digital-out states on the next controlLab pass.
    InputsWidget.markDirty();
    DigitalOutWidget.markDirty();
    InputsWidget.controlLab();
    // cannot run too often as this causes the usb sampling bug — the device
    // rebooted, so force a single resend of whatever gain the UI has selected
    // (survives reconnects; defaults to 16x)
    s_applied_gain = 0.0;
    applyScopeGain(OSCWidget.getSelectedGain());
}

void InstrumentFrontend::loadSettings(Settings& s)
{
    // setSelectedGain ignores values outside the hardware gain table, so a
    // hand-edited bogus value falls back to the compiled-in default.
    OSCWidget.setSelectedGain(s.getDouble("hw_gain", OSCWidget.getSelectedGain()));
    OSCWidget.AutoGain = s.getBool("hw_gain_auto", OSCWidget.AutoGain);

    // Stored calibration applies into librador's conversion buffers even
    // before a board connects.
    if (s.getBool("cal_scope_valid", false))
        CalibrationWidget.applyStored(s.getDouble("cal_vref1", 1.65),
            s.getDouble("cal_gain1", 1.0), s.getDouble("cal_vref2", 1.65),
            s.getDouble("cal_gain2", 1.0));
    if (s.getBool("cal_psu_valid", false))
        CalibrationWidget.applyStoredPsuOffset(s.getDouble("cal_psu_offset", 0.0));
}

void InstrumentFrontend::saveSettings(Settings& s)
{
    s.set("hw_gain", OSCWidget.getSelectedGain());
    s.set("hw_gain_auto", OSCWidget.AutoGain);

    if (CalibrationWidget.calibrationValid())
    {
        s.set("cal_scope_valid", true);
        s.set("cal_vref1", CalibrationWidget.vref(1));
        s.set("cal_gain1", CalibrationWidget.gainScale(1));
        s.set("cal_vref2", CalibrationWidget.vref(2));
        s.set("cal_gain2", CalibrationWidget.gainScale(2));
    }
    if (CalibrationWidget.psuOffsetValid())
    {
        s.set("cal_psu_valid", true);
        s.set("cal_psu_offset", CalibrationWidget.psuOffset());
    }
}

void InstrumentFrontend::UpdateHardwareState(App& app)
{
    if (app.isFlashing())
        return; // the flash/recovery worker owns the USB state right now
    if (app.isConnected())
    {
        // Sentinel variants 179/176 mean the board wedged mid-enumeration
        const uint8_t firmware_variant = librador_get_device_firmware_variant();
        if (firmware_variant == 179 || firmware_variant == 176)
        {
            librador_reset_usb();
            app.setConnected(false);
            return;
        }
        // The calibration wizard drives PSU voltage, device mode and gain
        // itself while active — keep the periodic senders off its lanes.
        const bool cal_wizard_active = CalibrationWidget.wizardActive();
        static bool cal_wizard_was_active = false;
        if (cal_wizard_was_active && !cal_wizard_active)
            InputsWidget.markDirty(); // wizard changed device modes directly
        cal_wizard_was_active = cal_wizard_active;

        if (app.frames() % app.labRefreshRate() == 0 && !cal_wizard_active)
        {
            PSUWidget.controlLab();
        }
        SG1Widget.controlLab();
        SG2Widget.controlLab();
        InputsWidget.controlLab();
        MultimeterWidget.controlLab();
        LogicWidget.controlLab();
        DAQWidget.controlLab();
        DigitalOutWidget.controlLab();
        CalibrationWidget.controlLab();
        // DAQReplayWidget is a pure viewer (controlLab is a no-op); it renders
        // via the widget list and needs no hardware servicing.

        // Hardware scope gain: apply manual combo changes (no-op unless the
        // value changed), then let auto-gain follow the visible plot range.
        if (!cal_wizard_active)
        {
            applyScopeGain(OSCWidget.getSelectedGain());
            if (OSCWidget.AutoGain)
                updateAutoGain(OSCWidget, InputsWidget, PlotWidgetObj);
        }
        else
        {
            // Resync our cache with whatever gain the wizard set
            s_applied_gain = librador_get_oscilloscope_gain();
            OSCWidget.setSelectedGain(s_applied_gain);
        }

        if (analysisToolsWidget.NA.Acquire)
        {
            na.StartSweep(na_cfg);
        }
        if (na.running())
        {
            na.Tick();
        }
    }
}

void InstrumentFrontend::RenderMenuBar(App& app)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Device"))
        {
            if (app.isConnected() && !app.isFlashing()) // no control transfers while a flash worker owns USB
            {
                uint16_t deviceVersion = librador_get_device_firmware_version();
                uint8_t deviceVariant = librador_get_device_firmware_variant();
                ImGui::TextColored(constants::GRAY_TEXT, "Firmware: %hu.%hhu",
                    deviceVersion, deviceVariant);
                static const char* transport_names[] = {"auto", "iso x6", "iso x1", "bulk"};
                int transport = librador_get_active_transport();
                uint64_t f_ok = 0, f_bad = 0, f_drop = 0, f_unval = 0;
                librador_get_frame_stats(&f_ok, &f_bad, &f_drop, &f_unval);
                ImGui::TextColored(constants::GRAY_TEXT, "Transport: %s",
                    (transport >= 0 && transport <= 3) ? transport_names[transport] : "?");
                ImGui::TextColored(constants::GRAY_TEXT,
                    "Frames ok/bad/lost: %llu / %llu / %llu",
                    (unsigned long long)f_ok, (unsigned long long)f_bad,
                    (unsigned long long)f_drop);
            }
            else if (app.isFlashing())
            {
                ImGui::TextColored(constants::GRAY_TEXT, "Firmware update in progress");
            }
            else
            {
                ImGui::TextColored(constants::GRAY_TEXT, "No Labrador board detected");
            }
#ifndef __ANDROID__
            ImGui::Separator();
            // Reuses the mismatch modal flow (App::drawFirmwarePopup opens it)
            if (ImGui::MenuItem("Reflash firmware", NULL, false, app.isConnected() && !app.isFlashing()))
                app.requestFirmwareReflash();
#endif
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("User Guide", NULL, &showHelpWindow);
            ImGui::MenuItem("Keyboard Shortcuts", "F1", &app.showShortcuts());
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            auto layoutItem = [&app](const char* label, App::LayoutMode mode) {
                if (ImGui::MenuItem(label, NULL, app.layoutMode() == mode))
                    app.setLayoutMode(mode);
            };
            layoutItem("Auto", App::LayoutMode::Auto);
            layoutItem("Desktop", App::LayoutMode::Desktop);
            layoutItem("Mobile", App::LayoutMode::Mobile);
            layoutItem("Compact (800x480)", App::LayoutMode::Compact);
            ImGui::Separator();
            if (ImGui::BeginMenu("Theme"))
            {
                for (int i = 0; i < ThemeCount(); i++)
                {
                    const ThemeSpec& t = ThemeAt(i);
                    if (ImGui::MenuItem(t.label, NULL, app.themeId() == t.id))
                        app.setThemeId(t.id);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Text Size"))
            {
                auto sizeItem = [&app](const char* label, float s) {
                    if (ImGui::MenuItem(
                            label, NULL, std::fabs(app.fontScale() - s) < 0.01f))
                        app.setFontScale(s);
                };
                sizeItem("Small", 0.85f);
                sizeItem("Normal", 1.0f);
                sizeItem("Large", 1.2f);
                sizeItem("Extra Large", 1.45f);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug"))
        {
            ImGui::MenuItem("Debug console", NULL, &app.showDebugConsole());
            ImGui::EndMenu();
        }

        if (app.isFlashing())
            TextRight(app.isRecovering()
                    ? "Recovering board from bootloader mode, do not unplug     "
                    : "Flashing firmware, do not unplug     ");
        else if (app.isConnected())
        {
            if (app.safetyMode())
                TextRight("Labrador In Safety Mode, Disconnect and Reconnect Device     ");
            else if (app.uninitialisedMode())
                TextRight("Labrador In Uninitialised State, Disconnect and Reconnect Device");
            else
                TextRight("Labrador Connected     ");
        }
        else if (app.bootloaderSeen())
            TextRight("Labrador In Bootloader Mode     ");
        else
            TextRight("No Labrador Found     ");

        const ImU32 status_colour = ImGui::GetColorU32(
            app.isConnected() && !app.safetyMode() && !app.uninitialisedMode()
                ? ImVec4(0, 1, 0, 1)
                : ImVec4(1, 0, 0, 1));
        const float radius = ImGui::GetTextLineHeight() * 0.4;
        const ImVec2 p1 = ImGui::GetCursorScreenPos();
        draw_list->AddCircleFilled(
            ImVec2(p1.x - 10, p1.y + ImGui::GetTextLineHeight() - radius), radius,
            status_colour);

        ImGui::EndMenuBar();
    }
}

void InstrumentFrontend::handleShortcuts(App& app)
{
    ImGuiIO& io = ImGui::GetIO();
    // Don't steal keys while the user is typing in a field, or while a
    // modal/popup owns input.
    if (io.WantTextInput || ImGui::IsAnyItemActive())
        return;

    const bool ctrl = io.KeyCtrl || io.KeySuper; // Cmd on macOS

    // Run/Stop — space (Qt had no direct key; this is the ImGui-native choice)
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        OSCWidget.Paused = !OSCWidget.Paused;

    // Vertical range ~ hardware gain (Qt up/down/w/s adjusted voltage range).
    // Up/w = more gain (smaller range), Down/s = less gain.
    auto stepGain = [this](int dir) {
        int idx = OSCControl::gainIndex(OSCWidget.getSelectedGain());
        if (idx < 0) idx = OSCWidget.GainComboCurrentItem;
        idx = idx + dir;
        if (idx < 0) idx = 0;
        if (idx > OSCControl::GainValueCount - 1) idx = OSCControl::GainValueCount - 1;
        OSCWidget.GainComboCurrentItem = idx;
        OSCWidget.AutoGain = false; // manual key overrides auto, like the combo
    };
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) || ImGui::IsKeyPressed(ImGuiKey_W, true))
        stepGain(+1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) || ImGui::IsKeyPressed(ImGuiKey_S, true))
        stepGain(-1);

    // Auto-fit — f (both axes), Qt used the menu; f is the ImGui-native pick
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        OSCWidget.AutofitX = true;
        OSCWidget.AutofitY = true;
    }

    // Cursors — 1 and 2 toggle the two cursors (Qt toggled via checkboxes)
    if (ImGui::IsKeyPressed(ImGuiKey_1, false))
        OSCWidget.Cursor1toggle = !OSCWidget.Cursor1toggle;
    if (ImGui::IsKeyPressed(ImGuiKey_2, false))
        OSCWidget.Cursor2toggle = !OSCWidget.Cursor2toggle;

    // Channel show/hide — c / v (Qt used c/v for snapshots; we have no
    // per-channel snapshot, so repurpose to the more useful show/hide)
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        OSCWidget.DisplayCheckOSC1 = !OSCWidget.DisplayCheckOSC1;
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        OSCWidget.DisplayCheckOSC2 = !OSCWidget.DisplayCheckOSC2;

    // Reconnect — Esc (Qt's Esc = reinitUsb)
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && librador_is_connected())
        librador_reset_usb();

    // Help — F1 or '?'
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        app.showShortcuts() = !app.showShortcuts();
}

void InstrumentFrontend::loadHelpFile()
{
    // In-app documentation parsed from the bundled help.md (the Monash README)
    std::vector<unsigned char> help_data = loadAsset("help.md");
    std::istringstream file(std::string(help_data.begin(), help_data.end()));

    std::stringstream buffer;
    std::string line;
    std::string curr_header = "";

    // Read until User Documentation section
    while (std::getline(file, line))
    {
        if (line == "# User Documentation")
            break;
    }

    while (std::getline(file, line))
    {
        if (line.compare(0, 4, "### ") == 0)
        {
            line.erase(0, 4);
            // Write the current buffer to the correct widget
            // Widget Label must be contained within the help header
            for (ControlWidget* w : widgets)
            {
                if ((w->getLabel()).find(curr_header) != std::string::npos)
                {
                    w->setHelpText(buffer.str());
                }
            }
            buffer.str("");
            buffer.clear();
            curr_header = line;
        }
        else if (line != "" && curr_header != "")
        {
            replace_all(line, "**", "");
            buffer << line << '\n';
        }
    }
}

void InstrumentFrontend::renderHelpWindow(bool* p_open)
{
    if (!ImGui::Begin("Labrador User Guide", p_open))
    {
        ImGui::End();
        return;
    }
    if (!ImGui::IsWindowFocused())
    {
        *p_open = false;
        ImGui::End();
        return;
    }

    static char search[50] = "";
    ImGui::InputTextWithHint("##help_search", "Search...", search, 50);
    ImGui::SameLine();
    bool expand = ImGui::Button("Expand all");
    ImGui::SameLine();
    bool collapse = ImGui::Button("Collapse all");
    std::string search_str = std::string(search);

    for (ControlWidget* w : widgets)
    {
        ImGui::SeparatorText(w->getLabel().c_str());
        w->renderHelpText(expand, collapse, search_str.length() > 2 ? search_str : "");
    }
    ImGui::End();
}

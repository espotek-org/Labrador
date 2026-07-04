#pragma once

#include "ui/Frontend.h"

// The instrument headers use ImGui/ImPlot internal APIs and rely on the
// including TU providing these first (same pattern as the Monash App.hpp).
#include "imgui_internal.h"
#include "implot.h"
#include "implot_internal.h"

#include "instruments/util.h"
#include "instruments/ControlWidget.hpp"
#include "instruments/PSUControl.hpp"
#include "instruments/SGControl.hpp"
#include "instruments/OSCControl.hpp"
#include "instruments/PlotWidget.hpp"
#include "instruments/OscData.hpp"
#include "instruments/AnalysisToolsWidget.hpp"
#include "instruments/NetworkAnalyser.hpp"
#include "instruments/InputsControl.hpp"
#include "instruments/MultimeterControl.hpp"
#include "instruments/LogicDecodeControl.hpp"
#include "instruments/DAQControl.hpp"
#include "instruments/DigitalOutControl.hpp"
#include "instruments/CalibrationControl.hpp"
#include "instruments/DAQReplay.hpp"

class App;
class Settings;

// Shared base for the "instrument" form factors (DesktopFrontend and
// LowResFrontend). Owns the full Monash control-widget set, the data those
// widgets share, the pinout textures, the desktop chrome (menu bar, keyboard
// shortcuts, help window) and the per-frame hardware servicing / auto-gain.
// A subclass supplies only renderLayout(): the actual widget arrangement.
//
// Everything here is a faithful lift of the machinery that used to live in the
// App god-class; the shared App is now just the session controller (SDL,
// librador, firmware/bootloader/gobindar flows, settings file, debug console).
class InstrumentFrontend : public Frontend
{
  public:
    void startUp(App& app) override;
    void update(App& app) override;
    void shutDown(App& app) override;
    void onDeviceConnected(App& app) override;
    void loadSettings(Settings& s) override;
    void saveSettings(Settings& s) override;

  protected:
    // The concrete widget arrangement for this form factor. Called inside an
    // ImGui frame by update(); expected to open the main window, draw the menu
    // bar (RenderMenuBar), render the widgets and call UpdateHardwareState.
    virtual void renderLayout(App& app) = 0;

    // Desktop chrome + servicing shared by every instrument layout.
    void RenderMenuBar(App& app);
    void handleShortcuts(App& app);
    void UpdateHardwareState(App& app);
    void loadHelpFile();
    void renderHelpWindow(bool* p_open);

    bool showHelpWindow = false;
    const float min_widget_width = 420.0f;

    // Widget set — identical construction to the Monash app
    PSUControl PSUWidget = PSUControl("Power Supply Unit (PSU)", ImVec2(0, 0), constants::PSU_ACCENT);
    SGControl SG1Widget = SGControl("Signal Generator 1 (SG1)", ImVec2(0, 0),
        constants::SG1_ACCENT, 1, &PSUWidget.voltage);
    SGControl SG2Widget = SGControl("Signal Generator 2 (SG2)", ImVec2(0, 0),
        constants::SG2_ACCENT, 2, &PSUWidget.voltage);
    OSCControl OSCWidget = OSCControl("Plot Settings", ImVec2(0, 0), constants::OSC_ACCENT);
    PlotWidget PlotWidgetObj = PlotWidget("Plot Window", ImVec2(0, 0), constants::PLOT_ACCENT);
    AnalysisToolsWidget analysisToolsWidget
        = AnalysisToolsWidget("Analysis Tools", ImVec2(0, 0), constants::OSC_ACCENT);
    InputsControl InputsWidget = InputsControl("Inputs", ImVec2(0, 0), constants::GEN_ACCENT);
    MultimeterControl MultimeterWidget
        = MultimeterControl("Multimeter", ImVec2(0, 0), constants::MATH_ACCENT);
    LogicDecodeControl LogicWidget = LogicDecodeControl(
        "Logic Analyzer / Decoder", ImVec2(0, 0), constants::SPECTRUM_ANALYSER_ACCENT);
    DAQControl DAQWidget
        = DAQControl("Data Logger (DAQ)", ImVec2(0, 0), constants::NETWORK_ANALYSER_ACCENT);
    DigitalOutControl DigitalOutWidget
        = DigitalOutControl("Digital Outputs", ImVec2(0, 0), constants::SG1_ACCENT);
    CalibrationControl CalibrationWidget
        = CalibrationControl("Calibration", ImVec2(0, 0), constants::SG2_ACCENT);
    DAQReplay DAQReplayWidget
        = DAQReplay("DAQ File Replay", ImVec2(0, 0), constants::NETWORK_ANALYSER_ACCENT);
    HelpWidget TroubleShoot = HelpWidget("Troubleshooting");
    HelpWidget GeneralHelp = HelpWidget("General");
    ControlWidget* widgets[15] = { &GeneralHelp, &TroubleShoot, &PSUWidget, &SG1Widget,
        &SG2Widget, &OSCWidget, &PlotWidgetObj, &analysisToolsWidget, &InputsWidget,
        &MultimeterWidget, &LogicWidget, &DAQWidget, &DigitalOutWidget,
        &CalibrationWidget, &DAQReplayWidget };

    // Data shared across widgets
    OscData OSC1Data = OscData(1);
    OscData OSC2Data = OscData(2);
    OscData MathData = OscData(0);
    SpectrumPlots spectrum_plots;
    NetworkPlots network_plots;
    NetworkAnalyser na;
    NetworkAnalyser::Config na_cfg;
};

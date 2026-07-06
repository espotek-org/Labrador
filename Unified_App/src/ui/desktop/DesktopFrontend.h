#pragma once

#include "ui/InstrumentFrontend.h"

// Ground-up desktop layout (2026 redesign, replacing the Monash stacked
// column). Menu-bar-first design:
//
//   ┌────────────────────────────────────────────────────────────┐
//   │ File  Device  Scope  Tools  View  Help                     │  menu bar
//   ├────────────────────────────────────────────────────────────┤
//   │ [Run] [Auto Fit] | Inputs mode | Gain | …          [Panel] │  toolbar
//   ├──────────────────────────────────┬──────────────────┬──────┤
//   │                                  │                  │ rail │
//   │            plot                  │    side panel    │  of  │
//   │                                  │  (one page at a  │ page │
//   │                                  │      time)       │ tabs │
//   ├──────────────────────────────────┴──────────────────┴──────┤
//   │ ● Connected · FW 7.2      mode · rate · gain · REC         │  status bar
//   └────────────────────────────────────────────────────────────┘
//
// The control widgets survive as state holders + section renderers; the
// permanent chrome (run/stop, gain, input mode, exports, calibration, views)
// moved into the menu bar / toolbar, Qt-desktop style.
class DesktopFrontend : public InstrumentFrontend
{
  public:
    void startUp(App& app) override;
    void loadSettings(Settings& s) override;
    void saveSettings(Settings& s) override;

  protected:
    void renderLayout(App& app) override;

  private:
    // Side panel pages, in rail order.
    enum Panel : int
    {
        PanelScope = 0,
        PanelSignals,
        PanelPSU,
        PanelMeter,
        PanelLogic,
        PanelDAQ,
        PanelAnalysis,
        PanelCount
    };
    static const char* panelName(int p);
    static const float* panelAccent(int p);

    void handleDesktopShortcuts();
    void renderDesktopMenuBar(App& app);
    void renderToolbar(App& app, float height);
    void renderRail(float height);
    void renderSidePanel(App& app, float height);
    void renderStatusBar(App& app);
    void renderCalibrationWindow();
    void renderAboutWindow();

    // Panel bodies
    void renderScopePanel();
    void renderSignalsPanel();
    void renderPSUPanel();
    void renderMeterPanel();
    void renderLogicPanel();
    void renderDAQPanel();
    void renderAnalysisPanel();

    // Open the side panel on a given page (menus/tools shortcuts).
    void showPanel(int p)
    {
        m_panel = p;
        m_sidebar_visible = true;
    }

    // File > Export helper: one MenuItem that saves x/y as CSV when clicked.
    void exportCsvMenuItem(const char* item_label, const std::vector<double>& x,
        const std::vector<double>& y, const char* x_header, const char* y_header);

    int m_panel = PanelScope;
    bool m_sidebar_visible = true;
    // Computed every frame from the text size (not user-resizeable, not
    // persisted): 440 px design width at the default text size.
    float m_sidebar_width = 440.0f;
    bool m_show_calibration = false;
    bool m_show_about = false;
    bool m_scanlines = true; // CRT themes only (View > Theme > CRT Scanlines)
};

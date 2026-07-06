#pragma once

#include "ui/InstrumentFrontend.h"

#include <functional>
#include <vector>

class App;

// From-scratch touchscreen UI for small (5-7") Raspberry Pi LCDs (800x480 /
// 1024x600-class). Not a rearrangement of the desktop widgets: the scope
// screen is plot-first with a rail of large fixed buttons (Run, Auto fit,
// SG1, SG2, PSU, Menu) and a strip of readout chips (CH1, CH2, Trig, Time,
// Gain) that open large touch panels — steppers and segmented buttons only,
// no combos, no text fields (numeric entry goes through an on-screen
// keypad). Long-tail instruments (Meter, Logic, DAQ, ...) live behind the
// Menu as full-screen pages hosting the classic widgets at full width.
//
// The Monash widgets still own all hardware state and servicing
// (InstrumentFrontend::UpdateHardwareState / controlLab); this class only
// replaces the presentation.
class LowResFrontend : public InstrumentFrontend
{
  public:
    void startUp(App& app) override;

  protected:
    void renderLayout(App& app) override;

  private:
    // Full-screen page: the scope screen, the menu grid, or one classic
    // widget hosted at full width.
    enum class Page
    {
        Scope,
        Menu,
        Meter,
        Logic,
        Daq,
        Replay,
        Analysis,
        Inputs,
        DigitalOut,
        Cal,
        ScopeSetup,
    };
    Page m_page = Page::Scope;

    // Touch panels (popups) on the scope screen.
    enum class Panel
    {
        None,
        CH1,
        CH2,
        Trig,
        Time,
        Gain,
        SG1,
        SG2,
        PSU,
    };
    Panel m_active_panel = Panel::None;
    bool m_panel_open_pending = false; // OpenPopup on the next renderPanels

    void requestPanel(Panel p)
    {
        m_active_panel = p;
        m_panel_open_pending = true;
    }

    // On-screen numeric keypad (SI-value entry without a text field: Pi
    // touchscreens have no keyboard, and InputText has the IME crash).
    struct KeypadUnit
    {
        const char* label;
        double multiplier;
    };
    struct Keypad
    {
        bool open_request = false;
        char title[48] = "";
        char buf[24] = "";
        double min = 0.0;
        double max = 0.0;
        std::vector<KeypadUnit> units;
        std::function<void(double)> commit;
    };
    Keypad m_keypad;

    void openKeypad(const char* title, double min, double max,
        std::initializer_list<KeypadUnit> units, std::function<void(double)> commit);
    // Renders the nested keypad popup; call inside the panel that opened it.
    void keypadPopup();

    // Scope screen
    void renderScopeScreen(App& app);
    void renderRail(App& app, float width);
    void renderChips(App& app, float height);
    void renderPanels(App& app);
    void sgPanelBody(SGControl& sg, const float* accent);

    // Other pages
    void renderMenuScreen(App& app);
    void renderWidgetPage(App& app);

    // Plot area rect of the current frame (popup anchoring).
    ImVec2 m_plot_min = ImVec2(0, 0);
    ImVec2 m_plot_max = ImVec2(0, 0);

    // Testing hook (LABRADOR_TOUCH_VIEW): open a panel/page on the first
    // frame so the headless screenshot matrix can verify them.
    bool m_env_view_checked = false;
    void applyEnvViewOnce();
};

// From-scratch touchscreen UI for small (5-7") Raspberry Pi LCDs (800x480 /
// 1024x600-class). See LowResFrontend.h for the design; everything here is
// sized for fingers: ~ScaledPx(44)+ hit targets, steppers and segmented
// buttons instead of combos and drags, and an on-screen keypad instead of
// text fields (Pi touchscreens have no keyboard, and InputText carries the
// IME crash).
#include "ui/lowres/LowResFrontend.h"
#include "app/App.h"
#include "instruments/UIComponents.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

// style.FontSizeBase at the top of the frame (PushFont rebases it, so it is
// captured once per renderLayout and all font sizes derive from this).
float g_font_base = 18.0f;

void PushTouchFont(float rel)
{
    ImGui::PushFont(NULL, g_font_base * rel);
}

ImVec4 AccentVec(const float* accent, float alpha)
{
    return ImVec4(accent[0], accent[1], accent[2], alpha);
}

// Centre `text` horizontally in [x0,x1] at height y, in the given font scale.
void DrawCentered(ImDrawList* draw, float x0, float x1, float y, float font_rel,
    ImU32 col, const char* text)
{
    PushTouchFont(font_rel);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    draw->AddText(ImVec2(x0 + (x1 - x0 - ts.x) * 0.5f, y), col, text);
    ImGui::PopFont();
}

struct TouchOpts
{
    const float* accent = nullptr; // fill/border tint when selected
    const char* sub = nullptr;     // small second line
    bool selected = false;
    bool enabled = true;
    bool underline = false;         // accent identity bar along the bottom
    const ImVec4* fill = nullptr;   // explicit fill (Run/Stop state colours)
    float label_rel = 1.0f;         // label font scale
};

// The one touch primitive: a large button drawn by hand so fills, accents
// and two-line labels look right in every theme. The id doubles as the QA
// DebugLabel (FuzzBlacklisted matches on it — keep "Quit"/"Reflash"/... in
// the ids of anything the fuzzer must not press).
bool TouchButton(const char* id, const char* label, ImVec2 size, const TouchOpts& o = {})
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);

    if (!o.enabled)
        ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(id, size);
    if (!o.enabled)
        ImGui::EndDisabled();
    const bool held = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    if (o.fill)
        fill = *o.fill;
    else if (o.selected && o.accent)
        fill = AccentVec(o.accent, 0.28f);
    if (held)
        fill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    else if (hovered)
    {
        const ImVec4 h = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        fill = ImVec4(fill.x * 0.6f + h.x * 0.4f, fill.y * 0.6f + h.y * 0.4f,
            fill.z * 0.6f + h.z * 0.4f, std::max(fill.w, h.w));
    }
    if (!o.enabled)
        fill.w *= 0.4f;

    const float rounding = ScaledPx(8.0f);
    draw->AddRectFilled(p0, p1, ImGui::GetColorU32(fill), rounding);
    const ImVec4 border = (o.selected && o.accent)
        ? AccentVec(o.accent, 1.0f)
        : ImGui::GetStyleColorVec4(ImGuiCol_Border);
    draw->AddRect(p0, p1, ImGui::GetColorU32(border), rounding, 0,
        o.selected ? 2.0f : 1.0f);
    if (o.underline && o.accent)
        draw->AddRectFilled(ImVec2(p0.x + rounding, p1.y - ScaledPx(4.0f)),
            ImVec2(p1.x - rounding, p1.y - ScaledPx(1.0f)),
            ImGui::GetColorU32(AccentVec(o.accent, 1.0f)), ScaledPx(2.0f));

    ImU32 text_col = ImGui::GetColorU32(o.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    if (o.sub)
    {
        PushTouchFont(o.label_rel);
        const float lh = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        PushTouchFont(0.72f);
        const float sh = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        const float top = p0.y + (size.y - lh - sh - ScaledPx(2.0f)) * 0.5f;
        DrawCentered(draw, p0.x, p1.x, top, o.label_rel, text_col, label);
        DrawCentered(draw, p0.x, p1.x, top + lh + ScaledPx(2.0f), 0.72f,
            ImGui::GetColorU32(ImGuiCol_TextDisabled), o.sub);
    }
    else
    {
        PushTouchFont(o.label_rel);
        const float lh = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        DrawCentered(draw, p0.x, p1.x, p0.y + (size.y - lh) * 0.5f, o.label_rel,
            text_col, label);
    }
    return clicked;
}

// Row of equal cells, one selected. Returns the (possibly new) selection.
int Segmented(const char* id, const char* const* options, int count, int current,
    ImVec2 size, const float* accent)
{
    ImGui::PushID(id);
    const float gap = ScaledPx(4.0f);
    const float cw = (size.x - gap * (count - 1)) / count;
    for (int i = 0; i < count; i++)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        TouchOpts o;
        o.accent = accent;
        o.selected = (i == current);
        if (TouchButton(options[i], options[i], ImVec2(cw, size.y), o))
            current = i;
        ImGui::PopID();
    }
    ImGui::PopID();
    return current;
}

// Label, [-] , tappable value readout, [+]. Returns -1/0/+1 for the step
// buttons (with hold-to-repeat); *value_tapped fires for the readout.
// `value_sub` puts the name inside the readout instead of a side label
// (compact two-column form, used by the SG panel).
int StepperRow(const char* id, const char* label, const char* value_text,
    float width, float height, bool* value_tapped, const float* accent = nullptr,
    const char* value_sub = nullptr)
{
    ImGui::PushID(id);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int step = 0;

    const float label_w = label ? ScaledPx(78.0f) : 0.0f;
    const float btn_w = ScaledPx(56.0f);
    const float spacings = label ? 3.0f : 2.0f;
    const float val_w
        = width - label_w - 2.0f * btn_w - spacings * ImGui::GetStyle().ItemSpacing.x;

    if (label)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        PushTouchFont(0.85f);
        draw->AddText(ImVec2(p.x, p.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), label);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(label_w, height));
        ImGui::SameLine();
    }

    TouchOpts step_opts;
    step_opts.label_rel = 1.3f;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (TouchButton("-", "\xe2\x88\x92", ImVec2(btn_w, height), step_opts))
        step = -1;
    ImGui::PopItemFlag();
    ImGui::SameLine();

    TouchOpts vo;
    vo.accent = accent;
    vo.selected = accent != nullptr;
    vo.label_rel = 1.05f;
    vo.sub = value_sub;
    if (TouchButton("value", value_text, ImVec2(val_w, height), vo) && value_tapped)
        *value_tapped = true;
    ImGui::SameLine();

    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (TouchButton("+", "+", ImVec2(btn_w, height), step_opts))
        step = +1;
    ImGui::PopItemFlag();

    ImGui::PopID();
    return step;
}

void PanelTitle(const char* text, const float* accent)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    PushTouchFont(0.95f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    if (accent)
        draw->AddRectFilled(ImVec2(p.x, p.y + ImGui::GetTextLineHeight() + ScaledPx(3.0f)),
            ImVec2(p.x + ScaledPx(46.0f), p.y + ImGui::GetTextLineHeight() + ScaledPx(6.0f)),
            ImGui::GetColorU32(AccentVec(accent, 1.0f)), ScaledPx(2.0f));
    ImGui::Dummy(ImVec2(0, ScaledPx(4.0f)));
}

// "1.23 kHz" / "45 mV" style formatting.
void FormatSI(double v, const char* unit, char* out, size_t n)
{
    const double a = std::fabs(v);
    const char* pre = "";
    double m = 1.0;
    if (a >= 0.9995e6) { pre = "M"; m = 1e6; }
    else if (a >= 0.9995e3) { pre = "k"; m = 1e3; }
    else if (a >= 1.0 || a == 0.0) { }
    else if (a >= 0.9995e-3) { pre = "m"; m = 1e-3; }
    else { pre = "\xc2\xb5"; m = 1e-6; }
    snprintf(out, n, "%.4g %s%s", v / m, pre, unit);
}

// Next/previous value in the 1-2-5 ladder (scope-style steps).
double Ladder125(double v, int dir, double lo, double hi)
{
    static const double mant[3] = { 1.0, 2.0, 5.0 };
    v = std::clamp(v, lo, hi);
    const int e = (int)std::floor(std::log10(std::max(v, 1e-12)) + 1e-9);
    const double base = std::pow(10.0, (double)e);
    const double m = v / base;
    int k = e * 3 + ((m < 1.5) ? 0 : (m < 3.5) ? 1 : (m < 7.5) ? 2 : 3);
    k += dir;
    const int kmod = ((k % 3) + 3) % 3;
    const int kdiv = (k - kmod) / 3;
    return std::clamp(mant[kmod] * std::pow(10.0, (double)kdiv), lo, hi);
}

// Linear step that is coarse on big values, fine on small ones (volts).
double VoltStep(double v)
{
    return std::fabs(v) < 1.0 ? 0.05 : 0.25;
}

} // namespace

// ---------------------------------------------------------------------------

void LowResFrontend::startUp(App& app)
{
    InstrumentFrontend::startUp(app);
    // The plot is the whole screen here; its help lives in Menu > Help and
    // the reserved under-plot button row would waste scarce vertical pixels.
    PlotWidgetObj.ShowHelpButton = false;
}

// Testing hook: LABRADOR_TOUCH_VIEW=<sg1|trig|menu|meter|keypad|...> opens a
// panel or page on the first frame, so the headless screenshot matrix can
// verify them without touch input (mirrors LABRADOR_WINDOW_SIZE).
void LowResFrontend::applyEnvViewOnce()
{
    if (m_env_view_checked)
        return;
    m_env_view_checked = true;
    const char* v = SDL_getenv("LABRADOR_TOUCH_VIEW");
    if (v == nullptr)
        return;
    const std::string s(v);
    if (s == "ch1") requestPanel(Panel::CH1);
    else if (s == "ch2") requestPanel(Panel::CH2);
    else if (s == "trig") requestPanel(Panel::Trig);
    else if (s == "time") requestPanel(Panel::Time);
    else if (s == "gain") requestPanel(Panel::Gain);
    else if (s == "sg1") requestPanel(Panel::SG1);
    else if (s == "sg2") requestPanel(Panel::SG2);
    else if (s == "psu") requestPanel(Panel::PSU);
    else if (s == "keypad")
    {
        requestPanel(Panel::SG1);
        openKeypad("Frequency (Hz)", 1.0, 1e6,
            { { "Hz", 1.0 }, { "kHz", 1e3 }, { "MHz", 1e6 } }, [](double) {});
    }
    else if (s == "menu") m_page = Page::Menu;
    else if (s == "meter") m_page = Page::Meter;
    else if (s == "logic") m_page = Page::Logic;
    else if (s == "daq") m_page = Page::Daq;
    else if (s == "replay") m_page = Page::Replay;
    else if (s == "analysis") m_page = Page::Analysis;
    else if (s == "inputs") m_page = Page::Inputs;
    else if (s == "do") m_page = Page::DigitalOut;
    else if (s == "cal") m_page = Page::Cal;
    else if (s == "scope_setup") m_page = Page::ScopeSetup;
    else
        fprintf(stderr, "LABRADOR_TOUCH_VIEW: unknown view '%s'\n", v);
}

void LowResFrontend::renderLayout(App& app)
{
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle style_backup = style;

    applyEnvViewOnce();

    // Base text bump for arm's-length reading. Everything else is authored
    // through ScaledPx, which tracks FontScaleMain (unlike PushFont, which
    // rebases style.FontSizeBase and would leave ScaledPx sizes small).
    style.FontScaleMain *= 1.25f;
    g_font_base = style.FontSizeBase;

    style.WindowPadding = ImVec2(6, 6);
    style.ItemSpacing = ImVec2(6, 6);
    style.FramePadding = ImVec2(10, 8);
    style.TouchExtraPadding = ImVec2(4, 4); // hit slop beyond the visuals

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    const ImGuiWindowFlags main_flags = ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("Main Window", NULL, main_flags);

    // ScaledPx needs the resolved (scaled) font — set these after Begin.
    style.GrabMinSize = ScaledPx(24.0f);
    style.ScrollbarSize = ScaledPx(20.0f);

    switch (m_page)
    {
    case Page::Scope:
        renderScopeScreen(app);
        renderPanels(app);
        break;
    case Page::Menu:
        renderMenuScreen(app);
        break;
    default:
        renderWidgetPage(app);
        break;
    }

    UpdateHardwareState(app);

    ImGui::End();

    style = style_backup;
}

// ---- Scope screen ----------------------------------------------------------

void LowResFrontend::renderScopeScreen(App& app)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float rail_w = ScaledPx(112.0f);
    const float chip_h = ScaledPx(52.0f);
    const ImGuiWindowFlags noscroll
        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::BeginChild("##touch_left",
        ImVec2(avail.x - rail_w - ImGui::GetStyle().ItemSpacing.x, 0),
        ImGuiChildFlags_None, noscroll);
    {
        const float plot_h
            = ImGui::GetContentRegionAvail().y - chip_h - ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##touch_plot", ImVec2(0, plot_h), ImGuiChildFlags_None, noscroll);
        m_plot_min = ImGui::GetWindowPos();
        m_plot_max = ImVec2(m_plot_min.x + ImGui::GetWindowSize().x,
            m_plot_min.y + ImGui::GetWindowSize().y);
        PlotWidgetObj.setSize(ImGui::GetContentRegionAvail());
        PlotWidgetObj.Render();
        ImGui::EndChild();

        renderChips(app, chip_h);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    renderRail(app, rail_w);
}

void LowResFrontend::renderChips(App& app, float height)
{
    (void)app;
    const float w = ImGui::GetContentRegionAvail().x;
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float cw = (w - 4.0f * sp) / 5.0f;
    char val[48];

    // CH1 / CH2
    for (int ch = 1; ch <= 2; ch++)
    {
        const bool shown = ch == 1 ? OSCWidget.DisplayCheckOSC1 : OSCWidget.DisplayCheckOSC2;
        const int atten = ch == 1 ? OSCWidget.AttenuationComboCH1 : OSCWidget.AttenuationComboCH2;
        const float* accent = ch == 1 ? (const float*)&OSCWidget.OSC1Colour.Value
                                      : (const float*)&OSCWidget.OSC2Colour.Value;
        if (!shown)
            snprintf(val, sizeof val, "OFF");
        else if (atten > 0)
            snprintf(val, sizeof val, "ON \xc2\xb7 %.0fx", OSCControl::AttenuationValues[atten]);
        else
            snprintf(val, sizeof val, "ON");
        TouchOpts o;
        o.accent = accent;
        o.sub = val;
        o.selected = m_active_panel == (ch == 1 ? Panel::CH1 : Panel::CH2);
        o.underline = shown;
        char id[8];
        snprintf(id, sizeof id, "CH%d", ch);
        if (TouchButton(id, id, ImVec2(cw, height), o))
            requestPanel(ch == 1 ? Panel::CH1 : Panel::CH2);
        ImGui::SameLine();
    }

    // Trigger
    {
        if (!OSCWidget.Trigger)
            snprintf(val, sizeof val, "OFF");
        else
        {
            static const char* dirs[4] = { "CH1\xe2\x86\x91", "CH1\xe2\x86\x93",
                "CH2\xe2\x86\x91", "CH2\xe2\x86\x93" };
            const char* d = dirs[std::clamp(OSCWidget.TriggerTypeComboCurrentItem, 0, 3)];
            if (OSCWidget.AutoTriggerLevel)
                snprintf(val, sizeof val, "%s AUTO", d);
            else
            {
                char lv[24];
                FormatSI(OSCWidget.TriggerLevel.getValue(), "V", lv, sizeof lv);
                snprintf(val, sizeof val, "%s %s", d, lv);
            }
        }
        TouchOpts o;
        o.accent = constants::GEN_ACCENT;
        o.sub = val;
        o.selected = m_active_panel == Panel::Trig;
        if (TouchButton("TRIG", "TRIG", ImVec2(cw, height), o))
            requestPanel(Panel::Trig);
        ImGui::SameLine();
    }

    // Time window
    {
        if (PlotWidgetObj.VisibleXValid)
            FormatSI(PlotWidgetObj.VisibleXMax - PlotWidgetObj.VisibleXMin, "s", val, sizeof val);
        else
            snprintf(val, sizeof val, "\xe2\x80\x94");
        TouchOpts o;
        o.accent = constants::OSC_ACCENT;
        o.sub = val;
        o.selected = m_active_panel == Panel::Time;
        if (TouchButton("TIME", "TIME", ImVec2(cw, height), o))
            requestPanel(Panel::Time);
        ImGui::SameLine();
    }

    // Hardware gain
    {
        snprintf(val, sizeof val, "%gx%s", OSCWidget.getSelectedGain(),
            OSCWidget.AutoGain ? " AUTO" : "");
        TouchOpts o;
        o.accent = constants::OSC_ACCENT;
        o.sub = val;
        o.selected = m_active_panel == Panel::Gain;
        if (TouchButton("GAIN", "GAIN", ImVec2(cw, height), o))
            requestPanel(Panel::Gain);
    }
}

void LowResFrontend::renderRail(App& app, float width)
{
    const ImGuiWindowFlags noscroll
        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##touch_rail", ImVec2(width, 0), ImGuiChildFlags_None, noscroll);

    // Connection status strip
    const float status_h = ScaledPx(20.0f);
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec4 col;
        const char* txt;
        if (app.isFlashing())
        {
            col = ImVec4(1.0f, 0.8f, 0.1f, 1.0f);
            txt = app.isRecovering() ? "RESCUE" : "FLASH";
        }
        else if (app.isConnected())
        {
            col = ImVec4(0.1f, 0.9f, 0.2f, 1.0f);
            txt = "USB OK";
        }
        else if (app.bootloaderSeen())
        {
            col = ImVec4(1.0f, 0.6f, 0.1f, 1.0f);
            txt = "BOOT";
        }
        else
        {
            col = ImVec4(0.9f, 0.15f, 0.15f, 1.0f);
            txt = "NO DEV";
        }
        const float r = status_h * 0.28f;
        draw->AddCircleFilled(ImVec2(p.x + r + 2.0f, p.y + status_h * 0.5f), r,
            ImGui::GetColorU32(col));
        PushTouchFont(0.72f);
        draw->AddText(ImVec2(p.x + 2.0f * r + ScaledPx(8.0f),
                          p.y + (status_h - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), txt);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, status_h));
    }

    const float sp = ImGui::GetStyle().ItemSpacing.y;
    const float btn_h
        = (ImGui::GetContentRegionAvail().y - 5.0f * sp) / 6.0f;
    const ImVec2 bsz(ImGui::GetContentRegionAvail().x, btn_h);
    char sub[48];

    // Run / Stop (acquisition)
    {
        const bool running = !OSCWidget.Paused;
        const ImVec4 fill = running ? ImVec4(0.05f, 0.45f, 0.10f, 1.0f)
                                    : ImVec4(0.55f, 0.08f, 0.08f, 1.0f);
        TouchOpts o;
        o.fill = &fill;
        if (TouchButton("run_stop", running ? "RUNNING" : "STOPPED", bsz, o))
            OSCWidget.Paused = !OSCWidget.Paused;
    }

    // Auto fit both axes
    TouchOpts auto_opts;
    auto_opts.sub = "fit view";
    if (TouchButton("AUTO", "AUTO", bsz, auto_opts))
    {
        OSCWidget.AutofitX = true;
        OSCWidget.AutofitY = true;
    }

    // Signal generators
    for (int ch = 1; ch <= 2; ch++)
    {
        SGControl& sg = ch == 1 ? SG1Widget : SG2Widget;
        const float* accent = ch == 1 ? constants::SG1_ACCENT : constants::SG2_ACCENT;
        if (sg.isActive())
        {
            char f[20];
            FormatSI(sg.currentSignal().frequencyValue().getValue(), "Hz", f, sizeof f);
            snprintf(sub, sizeof sub, "%.4s %s", sg.currentSignal().getLabel().c_str(), f);
        }
        else
            snprintf(sub, sizeof sub, "OFF");
        TouchOpts o;
        o.accent = accent;
        o.sub = sub;
        o.selected = sg.isActive();
        o.underline = true;
        char id[8];
        snprintf(id, sizeof id, "SG%d", ch);
        if (TouchButton(id, id, bsz, o))
            requestPanel(ch == 1 ? Panel::SG1 : Panel::SG2);
    }

    // Power supply
    {
        snprintf(sub, sizeof sub, "%.1f V", PSUWidget.voltage);
        TouchOpts o;
        o.accent = constants::PSU_ACCENT;
        o.sub = sub;
        o.underline = true;
        if (TouchButton("PSU", "PSU", bsz, o))
            requestPanel(Panel::PSU);
    }

    // Menu (everything else)
    if (TouchButton("MENU", "MENU", bsz))
        m_page = Page::Menu;

    ImGui::EndChild();
}

// ---- Touch panels -----------------------------------------------------------

static const char* PanelPopupId(int p)
{
    static const char* ids[] = { "", "##panel_ch1", "##panel_ch2", "##panel_trig",
        "##panel_time", "##panel_gain", "##panel_sg1", "##panel_sg2", "##panel_psu" };
    return ids[p];
}

void LowResFrontend::renderPanels(App& app)
{
    (void)app;
    if (m_active_panel == Panel::None)
        return;

    const char* id = PanelPopupId((int)m_active_panel);
    if (m_panel_open_pending)
    {
        ImGui::OpenPopup(id);
        m_panel_open_pending = false;
    }

    // Anchored over the plot, just above the chip strip. The SG panels run
    // two columns of steppers, so they get the wider format.
    const bool wide = m_active_panel == Panel::SG1 || m_active_panel == Panel::SG2;
    const float pw = std::min(
        ScaledPx(wide ? 560.0f : 410.0f), m_plot_max.x - m_plot_min.x - ScaledPx(8.0f));
    ImGui::SetNextWindowPos(
        ImVec2((m_plot_min.x + m_plot_max.x) * 0.5f, m_plot_max.y - ScaledPx(2.0f)),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(pw, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ScaledPx(12.0f), ScaledPx(12.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ScaledPx(8.0f), ScaledPx(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ScaledPx(10.0f));

    if (ImGui::BeginPopup(id))
    {
        const float row_h = ScaledPx(46.0f);
        const float w = ImGui::GetContentRegionAvail().x;
        bool tapped = false;
        static const char* off_on[2] = { "OFF", "ON" };

        switch (m_active_panel)
        {
        case Panel::CH1:
        case Panel::CH2:
        {
            const int ch = m_active_panel == Panel::CH1 ? 1 : 2;
            bool& shown = ch == 1 ? OSCWidget.DisplayCheckOSC1 : OSCWidget.DisplayCheckOSC2;
            int& atten = ch == 1 ? OSCWidget.AttenuationComboCH1 : OSCWidget.AttenuationComboCH2;
            float& offset = ch == 1 ? OSCWidget.OffsetCH1 : OSCWidget.OffsetCH2;
            const float* accent = ch == 1 ? (const float*)&OSCWidget.OSC1Colour.Value
                                          : (const float*)&OSCWidget.OSC2Colour.Value;
            PanelTitle(ch == 1 ? "Channel 1 (OSC1)" : "Channel 2 (OSC2)", accent);

            shown = Segmented("show", off_on, 2, shown ? 1 : 0, ImVec2(w, row_h), accent) == 1;

            static const char* attens[3] = { "Probe 1x", "Probe 5x", "Probe 10x" };
            atten = Segmented("atten", attens, 3, atten, ImVec2(w, row_h), accent);

            char v[24];
            FormatSI(offset, "V", v, sizeof v);
            const int step = StepperRow("offset", "Offset", v, w, row_h, &tapped);
            if (step != 0)
                offset = std::clamp(offset + (float)step * (float)VoltStep(offset), -20.0f, 20.0f);
            if (tapped)
            {
                float* target = &offset;
                openKeypad(ch == 1 ? "CH1 offset (V)" : "CH2 offset (V)", -20.0, 20.0,
                    { { "mV", 1e-3 }, { "V", 1.0 } },
                    [target](double nv) { *target = (float)nv; });
            }
            break;
        }

        case Panel::Trig:
        {
            PanelTitle("Trigger", constants::GEN_ACCENT);
            OSCWidget.Trigger
                = Segmented("trig_on", off_on, 2, OSCWidget.Trigger ? 1 : 0,
                      ImVec2(w, row_h), constants::GEN_ACCENT)
                == 1;

            static const char* types[4] = { "CH1 \xe2\x86\x91", "CH1 \xe2\x86\x93",
                "CH2 \xe2\x86\x91", "CH2 \xe2\x86\x93" };
            OSCWidget.TriggerTypeComboCurrentItem = Segmented("trig_type", types, 4,
                OSCWidget.TriggerTypeComboCurrentItem, ImVec2(w, row_h), constants::GEN_ACCENT);

            static const char* level_modes[2] = { "MANUAL", "AUTO LEVEL" };
            OSCWidget.AutoTriggerLevel = Segmented("trig_auto", level_modes, 2,
                OSCWidget.AutoTriggerLevel ? 1 : 0, ImVec2(w, row_h), constants::GEN_ACCENT)
                == 1;

            if (!OSCWidget.AutoTriggerLevel)
            {
                char v[24];
                FormatSI(OSCWidget.TriggerLevel.getValue(), "V", v, sizeof v);
                const int step = StepperRow("level", "Level", v, w, row_h, &tapped);
                if (step != 0)
                    OSCWidget.TriggerLevel.setLevel(std::clamp(
                        OSCWidget.TriggerLevel.getValue() + 0.1f * (float)step,
                        OSCWidget.TriggerLevel.getMin(), OSCWidget.TriggerLevel.getMax()));
                if (tapped)
                {
                    OSCControl* osc = &OSCWidget;
                    openKeypad("Trigger level (V)", OSCWidget.TriggerLevel.getMin(),
                        OSCWidget.TriggerLevel.getMax(), { { "mV", 1e-3 }, { "V", 1.0 } },
                        [osc](double nv) { osc->TriggerLevel.setLevel((float)nv); });
                }
            }
            break;
        }

        case Panel::Time:
        {
            PanelTitle("Time window", constants::OSC_ACCENT);
            char v[24];
            double span = PlotWidgetObj.VisibleXValid
                ? PlotWidgetObj.VisibleXMax - PlotWidgetObj.VisibleXMin
                : 0.025;
            FormatSI(span, "s", v, sizeof v);
            const int step = StepperRow("span", "Window", v, w, row_h, &tapped);
            if (step != 0)
                PlotWidgetObj.PendingXSpan = Ladder125(span, step, 50e-6, 20.0);
            if (tapped)
            {
                PlotWidget* plot = &PlotWidgetObj;
                openKeypad("Time window (s)", 50e-6, 20.0,
                    { { "\xc2\xb5s", 1e-6 }, { "ms", 1e-3 }, { "s", 1.0 } },
                    [plot](double nv) { plot->PendingXSpan = nv; });
            }

            if (TouchButton("Fit time axis", "FIT TIME AXIS", ImVec2(w, row_h)))
            {
                OSCWidget.AutofitX = true;
                ImGui::CloseCurrentPopup();
            }

            // Measurement cursors (readouts appear under the plot)
            const float half = (w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            TouchOpts c1;
            c1.accent = constants::GEN_ACCENT;
            c1.selected = OSCWidget.Cursor1toggle;
            if (TouchButton("Cursor 1", "CURSOR 1", ImVec2(half, row_h), c1))
                OSCWidget.Cursor1toggle = !OSCWidget.Cursor1toggle;
            ImGui::SameLine();
            TouchOpts c2;
            c2.accent = constants::GEN_ACCENT;
            c2.selected = OSCWidget.Cursor2toggle;
            if (TouchButton("Cursor 2", "CURSOR 2", ImVec2(half, row_h), c2))
                OSCWidget.Cursor2toggle = !OSCWidget.Cursor2toggle;
            break;
        }

        case Panel::Gain:
        {
            PanelTitle("Input range (hardware gain)", constants::OSC_ACCENT);

            static const char* gain_modes[2] = { "MANUAL", "AUTO" };
            OSCWidget.AutoGain = Segmented("gain_auto", gain_modes, 2,
                OSCWidget.AutoGain ? 1 : 0, ImVec2(w, row_h), constants::OSC_ACCENT)
                == 1;

            char v[32];
            snprintf(v, sizeof v, "%gx  (\xc2\xb1%.3g V)", OSCWidget.getSelectedGain(),
                23.65 / OSCWidget.getSelectedGain());
            const int step = StepperRow("gain", "Gain", v, w, row_h, &tapped);
            if (step != 0)
            {
                OSCWidget.GainComboCurrentItem = std::clamp(
                    OSCWidget.GainComboCurrentItem + step, 0, OSCControl::GainValueCount - 1);
                OSCWidget.AutoGain = false; // manual step overrides auto, like the combo
            }

            if (TouchButton("Fit voltage axis", "FIT VOLTAGE AXIS", ImVec2(w, row_h)))
            {
                OSCWidget.AutofitY = true;
                ImGui::CloseCurrentPopup();
            }
            break;
        }

        case Panel::SG1:
            sgPanelBody(SG1Widget, constants::SG1_ACCENT);
            break;
        case Panel::SG2:
            sgPanelBody(SG2Widget, constants::SG2_ACCENT);
            break;

        case Panel::PSU:
        {
            PanelTitle("Power supply", constants::PSU_ACCENT);
            char v[24];
            snprintf(v, sizeof v, "%.1f V", PSUWidget.voltage);
            const int step
                = StepperRow("psu_v", "Voltage", v, w, row_h, &tapped, constants::PSU_ACCENT);
            if (step != 0)
                PSUWidget.voltage
                    = std::clamp(PSUWidget.voltage + 0.1f * (float)step, 4.5f, 11.0f);
            if (tapped)
            {
                PSUControl* psu = &PSUWidget;
                openKeypad("PSU voltage (V)", 4.5, 11.0, { { "V", 1.0 } },
                    [psu](double nv) { psu->voltage = (float)nv; });
            }
            PushTouchFont(0.72f);
            ImGui::TextDisabled("Output is live whenever a board is connected.");
            ImGui::PopFont();
            break;
        }

        default:
            break;
        }

        keypadPopup();
        ImGui::EndPopup();
    }
    else
    {
        m_active_panel = Panel::None;
    }
    ImGui::PopStyleVar(3);
}

void LowResFrontend::sgPanelBody(SGControl& sg, const float* accent)
{
    const float w = ImGui::GetContentRegionAvail().x;
    const float row_h = ScaledPx(46.0f);
    bool tapped = false;

    PanelTitle(&sg == &SG1Widget ? "Signal generator 1" : "Signal generator 2", accent);

    static const char* off_on[2] = { "OFF", "ON" };
    const bool want_on
        = Segmented("sg_on", off_on, 2, sg.isActive() ? 1 : 0, ImVec2(w, row_h), accent) == 1;
    sg.setActive(want_on);

    GenericSignal& sig = sg.currentSignal();

    // Live waveform preview (reuses the widget's preview plot)
    {
        sig.setPreviewLineColor(ImVec4(accent[0] * 1.4f, accent[1] * 1.4f, accent[2] * 1.4f, 1.0f));
        ImPlotStyle style_backup = ImPlot::GetStyle();
        PreviewStyle();
        if (ImPlot::BeginPlot("##sg_touch_preview", ImVec2(w, ScaledPx(56.0f)),
                ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs))
            sig.renderPreview();
        GImPlot->Style = style_backup;
    }

    // Waveform pick: the four procedural shapes plus a "more" cell for the
    // bundled arbitrary waveforms (DC, PRBS5, ...).
    {
        const int idx = sg.signalIndex();
        std::string more_label = idx >= 4 ? sg.signalLabel(idx) : "More \xe2\x96\xbe";
        const char* waves[5]
            = { "Sine", "Square", "Saw", "Tri", more_label.c_str() };
        const int pick = Segmented("sg_wave", waves, 5, idx < 4 ? idx : 4, ImVec2(w, row_h), accent);
        if (pick < 4)
            sg.selectSignal(pick);
        else if (pick == 4 && idx < 4)
            ImGui::OpenPopup("##sg_wave_more");
        if (pick == 4 && idx >= 4 && ImGui::IsItemClicked())
            ImGui::OpenPopup("##sg_wave_more");
        if (ImGui::BeginPopup("##sg_wave_more"))
        {
            for (int i = 4; i < sg.signalCount(); i++)
            {
                TouchOpts o;
                o.accent = accent;
                o.selected = i == sg.signalIndex();
                if (TouchButton(sg.signalLabel(i).c_str(), sg.signalLabel(i).c_str(),
                        ImVec2(ScaledPx(180.0f), row_h), o))
                {
                    sg.selectSignal(i);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    // Steppers in a two-column grid (the name rides inside the readout), so
    // the whole panel fits a 480 px screen without scrolling.
    char v[24];
    SGControl* sgp = &sg;
    const float grid_h = ScaledPx(52.0f);
    const float half = (w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // Frequency (1-2-5 ladder)
    {
        SIValue& f = sig.frequencyValue();
        FormatSI(f.getValue(), "Hz", v, sizeof v);
        const int step
            = StepperRow("sg_freq", nullptr, v, half, grid_h, &tapped, nullptr, "FREQUENCY");
        if (step != 0)
        {
            f.setLevel((float)Ladder125(f.getValue(), step, f.getMin(), f.getMax()));
            sg.markSwitched();
        }
        if (tapped)
            openKeypad("Frequency (Hz)", f.getMin(), f.getMax(),
                { { "Hz", 1.0 }, { "kHz", 1e3 }, { "MHz", 1e6 } },
                [sgp](double nv) {
                    sgp->currentSignal().frequencyValue().setLevel((float)nv);
                    sgp->markSwitched();
                });
    }
    ImGui::SameLine();

    // Amplitude
    {
        tapped = false;
        SIValue& a = sig.amplitudeValue();
        FormatSI(a.getValue(), "V", v, sizeof v);
        const int step
            = StepperRow("sg_amp", nullptr, v, half, grid_h, &tapped, nullptr, "AMPLITUDE Vpp");
        if (step != 0)
        {
            a.setLevel(std::clamp(a.getValue() + (float)step * (float)VoltStep(a.getValue()),
                a.getMin(), a.getMax()));
            sg.markSwitched();
        }
        if (tapped)
            openKeypad("Amplitude Vpp (V)", a.getMin(), a.getMax(),
                { { "mV", 1e-3 }, { "V", 1.0 } }, [sgp](double nv) {
                    sgp->currentSignal().amplitudeValue().setLevel((float)nv);
                    sgp->markSwitched();
                });
    }

    // Offset (Vbase)
    {
        tapped = false;
        SIValue& o = sig.offsetValue();
        FormatSI(o.getValue(), "V", v, sizeof v);
        const int step
            = StepperRow("sg_off", nullptr, v, half, grid_h, &tapped, nullptr, "OFFSET Vbase");
        if (step != 0)
        {
            o.setLevel(std::clamp(o.getValue() + (float)step * (float)VoltStep(o.getValue()),
                o.getMin(), o.getMax()));
            sg.markSwitched();
        }
        if (tapped)
            openKeypad("Offset Vbase (V)", o.getMin(), o.getMax(),
                { { "mV", 1e-3 }, { "V", 1.0 } }, [sgp](double nv) {
                    sgp->currentSignal().offsetValue().setLevel((float)nv);
                    sgp->markSwitched();
                });
    }

    // Duty cycle (square only)
    if (sig.hasDutyCycle())
    {
        ImGui::SameLine();
        snprintf(v, sizeof v, "%d %%", sig.dutyCycle());
        const int step
            = StepperRow("sg_duty", nullptr, v, half, grid_h, nullptr, nullptr, "DUTY CYCLE");
        if (step != 0)
        {
            sig.setDutyCycle(sig.dutyCycle() + 5 * step);
            sg.markSwitched();
        }
    }
}

// ---- Keypad -----------------------------------------------------------------

void LowResFrontend::openKeypad(const char* title, double min, double max,
    std::initializer_list<KeypadUnit> units, std::function<void(double)> commit)
{
    snprintf(m_keypad.title, sizeof m_keypad.title, "%s", title);
    m_keypad.buf[0] = '\0';
    m_keypad.min = min;
    m_keypad.max = max;
    m_keypad.units.assign(units);
    m_keypad.commit = std::move(commit);
    m_keypad.open_request = true;
}

void LowResFrontend::keypadPopup()
{
    if (m_keypad.open_request)
    {
        ImGui::OpenPopup("##touch_keypad");
        m_keypad.open_request = false;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopup("##touch_keypad"))
        return;

    const float key = ScaledPx(58.0f);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float grid_w = 4.0f * key + 3.0f * gap;

    PushTouchFont(0.8f);
    ImGui::TextDisabled("%s", m_keypad.title);
    ImGui::PopFont();

    // Entry readout
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ScaledPx(44.0f);
        draw->AddRectFilled(p, ImVec2(p.x + grid_w, p.y + h),
            ImGui::GetColorU32(ImGuiCol_FrameBg), ScaledPx(6.0f));
        PushTouchFont(1.25f);
        const char* shown = m_keypad.buf[0] ? m_keypad.buf : "0";
        const ImVec2 ts = ImGui::CalcTextSize(shown);
        draw->AddText(ImVec2(p.x + grid_w - ts.x - ScaledPx(10.0f),
                          p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), shown);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(grid_w, h));
    }

    auto append = [this](char c) {
        const size_t len = strlen(m_keypad.buf);
        if (len >= sizeof(m_keypad.buf) - 2)
            return;
        if (c == '.' && strchr(m_keypad.buf, '.'))
            return;
        m_keypad.buf[len] = c;
        m_keypad.buf[len + 1] = '\0';
    };
    auto commitWith = [this](double mult) {
        const double nv = std::clamp(atof(m_keypad.buf) * mult, m_keypad.min, m_keypad.max);
        if (m_keypad.commit)
            m_keypad.commit(nv);
        ImGui::CloseCurrentPopup();
    };

    // 3x4 digit grid + right column: backspace and the unit-commit keys
    // (type "1.5" then tap "kHz" — scope-keypad convention).
    static const char* digit_rows[4][3]
        = { { "7", "8", "9" }, { "4", "5", "6" }, { "1", "2", "3" }, { "\xc2\xb1", "0", "." } };
    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            if (c > 0)
                ImGui::SameLine();
            const char* k = digit_rows[r][c];
            TouchOpts ko;
            ko.label_rel = 1.15f;
            if (TouchButton(k, k, ImVec2(key, key), ko))
            {
                if (k[0] == '.')
                    append('.');
                else if ((unsigned char)k[0] >= '0' && (unsigned char)k[0] <= '9')
                    append(k[0]);
                else // toggle sign
                {
                    if (m_keypad.buf[0] == '-')
                        memmove(m_keypad.buf, m_keypad.buf + 1, strlen(m_keypad.buf));
                    else if (strlen(m_keypad.buf) < sizeof(m_keypad.buf) - 2)
                    {
                        memmove(m_keypad.buf + 1, m_keypad.buf, strlen(m_keypad.buf) + 1);
                        m_keypad.buf[0] = '-';
                    }
                }
            }
        }
        ImGui::SameLine();
        if (r == 0)
        {
            TouchOpts ko;
            ko.label_rel = 1.15f;
            if (TouchButton("backspace", "\xe2\x8c\xab", ImVec2(key, key), ko))
            {
                const size_t len = strlen(m_keypad.buf);
                if (len > 0)
                    m_keypad.buf[len - 1] = '\0';
            }
        }
        else
        {
            const int ui = r - 1;
            if (ui < (int)m_keypad.units.size())
            {
                TouchOpts o;
                o.accent = constants::GEN_ACCENT;
                o.selected = true;
                if (TouchButton(m_keypad.units[ui].label, m_keypad.units[ui].label,
                        ImVec2(key, key), o))
                    commitWith(m_keypad.units[ui].multiplier);
            }
            else
            {
                ImGui::Dummy(ImVec2(key, key));
            }
        }
    }

    ImGui::EndPopup();
}

// ---- Menu + classic widget pages ---------------------------------------------

void LowResFrontend::renderMenuScreen(App& app)
{
    // Sized so all five rows fit a 480 px screen; the body still scrolls as
    // the fallback at larger text sizes.
    const float row_h = ScaledPx(46.0f);

    // Header: back + title + device status
    if (TouchButton("Back", "\xe2\x97\x80 BACK", ImVec2(ScaledPx(110.0f), ScaledPx(44.0f))))
        m_page = Page::Scope;
    ImGui::SameLine();
    PushTouchFont(1.1f);
    ImGui::TextUnformatted("Menu");
    ImGui::PopFont();
    ImGui::SameLine();
    {
        char status[96];
        if (app.isConnected() && !app.isFlashing())
            snprintf(status, sizeof status, "Labrador connected \xc2\xb7 firmware %hu.%hhu",
                librador_get_device_firmware_version(), librador_get_device_firmware_variant());
        else if (app.isFlashing())
            snprintf(status, sizeof status, "Firmware update in progress");
        else
            snprintf(status, sizeof status, "No Labrador board detected");
        PushTouchFont(0.75f);
        TextRight(status);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, ScaledPx(2.0f)));

    ImGui::BeginChild("##menu_body", ImVec2(0, 0));
    const float w = ImGui::GetContentRegionAvail().x;
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float cw = (w - 3.0f * sp) / 4.0f;

    struct Cell
    {
        const char* id;
        const char* label;
        const char* sub;
        Page page;
    };
    static const Cell instruments[] = {
        { "Meter", "METER", "multimeter", Page::Meter },
        { "Logic", "LOGIC", "analyzer / decode", Page::Logic },
        { "DAQ", "DAQ", "data logger", Page::Daq },
        { "Replay", "REPLAY", "DAQ files", Page::Replay },
        { "Analysis", "ANALYSIS", "FFT / network", Page::Analysis },
        { "Inputs", "INPUTS", "device mode", Page::Inputs },
        { "Digital Out", "DIGITAL OUT", "DO1-DO4", Page::DigitalOut },
        { "Calibrate", "CALIBRATE", "scope / PSU", Page::Cal },
        { "Scope Setup", "SCOPE SETUP", "views / export", Page::ScopeSetup },
    };
    int col = 0;
    for (const Cell& c : instruments)
    {
        if (col == 4)
            col = 0;
        if (col++)
            ImGui::SameLine();
        TouchOpts o;
        o.sub = c.sub;
        if (TouchButton(c.id, c.label, ImVec2(cw, row_h), o))
            m_page = c.page;
    }

    // Theme cycle
    {
        if (col == 4)
            col = 0;
        if (col++)
            ImGui::SameLine();
        const ThemeSpec* cur = FindTheme(app.themeId());
        TouchOpts o;
        o.sub = cur ? cur->label : "?";
        if (TouchButton("Theme", "THEME", ImVec2(cw, row_h), o))
        {
            int i = 0;
            for (; i < ThemeCount(); i++)
                if (app.themeId() == ThemeAt(i).id)
                    break;
            app.setThemeId(ThemeAt((i + 1) % ThemeCount()).id);
        }
    }

    // Text size cycle
    {
        if (col == 4)
            col = 0;
        if (col++)
            ImGui::SameLine();
        static const float sizes[4] = { 0.85f, 1.0f, 1.2f, 1.45f };
        static const char* size_names[4] = { "small", "normal", "large", "extra large" };
        int cur = 1;
        for (int i = 0; i < 4; i++)
            if (std::fabs(app.fontScale() - sizes[i]) < 0.01f)
                cur = i;
        TouchOpts o;
        o.sub = size_names[cur];
        if (TouchButton("Text Size", "TEXT SIZE", ImVec2(cw, row_h), o))
            app.setFontScale(sizes[(cur + 1) % 4]);
    }

    // Help
    {
        if (col == 4)
            col = 0;
        if (col++)
            ImGui::SameLine();
        TouchOpts o;
        o.sub = "user guide";
        if (TouchButton("Help", "HELP", ImVec2(cw, row_h), o))
            showHelpWindow = true;
    }

    ImGui::Dummy(ImVec2(0, ScaledPx(6.0f)));
    PushTouchFont(0.75f);
    ImGui::TextDisabled("Switch layout");
    ImGui::PopFont();
    {
        struct Lay
        {
            const char* label;
            App::LayoutMode mode;
        };
        static const Lay lays[] = { { "Desktop", App::LayoutMode::Desktop },
            { "Tablet", App::LayoutMode::Tablet }, { "Mobile", App::LayoutMode::Mobile },
            { "Auto", App::LayoutMode::Auto } };
        for (int i = 0; i < 4; i++)
        {
            if (i)
                ImGui::SameLine();
            const bool current = app.layoutMode() == lays[i].mode;
            TouchOpts o;
            o.accent = constants::GEN_ACCENT;
            o.selected = current;
            if (TouchButton(lays[i].label, lays[i].label, ImVec2(cw, row_h), o))
            {
                app.setLayoutMode(lays[i].mode);
                m_page = Page::Scope; // come back to the scope screen next time
            }
        }
    }

    ImGui::Dummy(ImVec2(0, ScaledPx(6.0f)));
    {
        // "Reflash"/"Quit" in the ids keeps these on the QA fuzz blacklist.
        TouchOpts reflash;
        reflash.sub = "stock 7.2";
        reflash.enabled = app.isConnected() && !app.isFlashing();
        if (TouchButton("Reflash firmware", "REFLASH FIRMWARE", ImVec2(cw, row_h), reflash))
            app.requestFirmwareReflash();
        ImGui::SameLine();
        TouchOpts dbg;
        dbg.sub = "console";
        dbg.selected = app.showDebugConsole();
        dbg.accent = constants::GEN_ACCENT;
        if (TouchButton("Debug console", "DEBUG", ImVec2(cw, row_h), dbg))
            app.showDebugConsole() = !app.showDebugConsole();
        ImGui::SameLine();
        TouchOpts quit_opts;
        quit_opts.sub = "exit app";
        if (TouchButton("Quit", "QUIT", ImVec2(cw, row_h), quit_opts))
            app.requestQuit();
    }

    ImGui::EndChild();
}

void LowResFrontend::renderWidgetPage(App& app)
{
    (void)app;
    const char* title = "";
    ControlWidget* widget = nullptr;
    bool with_plot = false;

    switch (m_page)
    {
    case Page::Meter: title = "Multimeter"; widget = &MultimeterWidget; break;
    case Page::Logic: title = "Logic Analyzer / Decoder"; widget = &LogicWidget; break;
    case Page::Daq: title = "Data Logger (DAQ)"; widget = &DAQWidget; break;
    case Page::Replay: title = "DAQ File Replay"; widget = &DAQReplayWidget; break;
    case Page::Analysis:
        title = "Analysis (FFT / Network)";
        widget = &analysisToolsWidget;
        with_plot = true;
        break;
    case Page::Inputs: title = "Inputs / Device Mode"; widget = &InputsWidget; break;
    case Page::DigitalOut: title = "Digital Outputs"; widget = &DigitalOutWidget; break;
    case Page::Cal: title = "Calibration"; widget = &CalibrationWidget; break;
    case Page::ScopeSetup:
        title = "Scope Setup";
        widget = &OSCWidget;
        with_plot = true;
        break;
    default:
        m_page = Page::Scope;
        return;
    }

    if (TouchButton("Back", "\xe2\x97\x80 BACK", ImVec2(ScaledPx(110.0f), ScaledPx(44.0f))))
        m_page = Page::Menu;
    ImGui::SameLine();
    PushTouchFont(1.1f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, ScaledPx(2.0f)));

    // The multimeter only measures in its own device mode; give the page a
    // one-tap way in and out instead of a detour through the Inputs page.
    if (m_page == Page::Meter)
    {
        const bool meter_mode = InputsWidget.mode() == InputsControl::Mode::Multimeter;
        TouchOpts o;
        o.accent = constants::MATH_ACCENT;
        o.selected = meter_mode;
        o.sub = meter_mode ? "back to CH1+CH2 oscilloscope" : "device mode is not Multimeter";
        if (TouchButton("meter_mode",
                meter_mode ? "LEAVE MULTIMETER MODE" : "SWITCH TO MULTIMETER MODE",
                ImVec2(ImGui::GetContentRegionAvail().x, ScaledPx(52.0f)), o))
            InputsWidget.setMode(meter_mode ? InputsControl::Mode::ScopeScope
                                            : InputsControl::Mode::Multimeter);
    }

    // Pages whose settings change what the plot shows keep a live strip of it.
    if (with_plot)
    {
        const float plot_h = ImGui::GetContentRegionAvail().y * 0.42f;
        ImGui::BeginChild("##page_plot", ImVec2(0, plot_h), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        PlotWidgetObj.setSize(ImGui::GetContentRegionAvail());
        PlotWidgetObj.Render();
        ImGui::EndChild();
    }

    ImGui::BeginChild("##page_body", ImVec2(0, 0));
    widget->setSize(ImVec2(0, 0));
    widget->Render();
    ImGui::EndChild();
}

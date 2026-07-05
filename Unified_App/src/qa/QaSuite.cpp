// Dear ImGui Test Engine integration + the registered test suite.
// Compiled into every build but empty unless LABRADOR_QA is defined (the
// app target globs src/*.cpp unconditionally).
#ifdef LABRADOR_QA

#include "qa/QaSuite.h"

#include "imgui.h"
#include "imgui_internal.h" // IM_PI, ImMin/ImMax
#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
#include "librador.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static ImGuiTestEngine* g_engine = nullptr;
static bool g_headless = false;

// ---- The test suite ---------------------------------------------------------
// Conventions: tests drive the DESKTOP layout (main() forces it for --qa) and
// interact through the UI like a user would — click by label path, verify
// through menu checkmarks / item existence. Hardware ("hw/") tests talk to
// librador directly and expect the loopback harness: SG1->OSC1, SG2->OSC2.

// Test prologue: everything lives inside "Main Window"; close any popup a
// previous (possibly failed) test left open so item searches see the world.
static void SetMainRef(ImGuiTestContext* ctx)
{
    ctx->PopupCloseAll();
    ctx->SetRef("Main Window");
}

// Park the simulated mouse over the plot area (the app is one fullscreen
// window, so the engine's MouseMoveToVoid has no empty space to use).
static void ParkMouse(ImGuiTestContext* ctx)
{
    ImGuiWindow* window = ctx->GetWindowByRef("//Main Window");
    if (window != nullptr)
    {
        const ImRect r = window->Rect();
        ctx->MouseMoveToPos(ImVec2(r.Min.x + r.GetWidth() * 0.3f,
            r.Min.y + r.GetHeight() * 0.5f));
    }
}

// The app's toggles (ToggleSwitch, toolbar buttons, run/stop) report their
// state through ImGuiItemStatusFlags_Checked, like a checkbox would.
static bool ItemChecked(ImGuiTestContext* ctx, const char* path)
{
    ImGuiTestItemInfo info = ctx->ItemInfo(path);
    return (info.ID != 0) && (info.StatusFlags & ImGuiItemStatusFlags_Checked) != 0;
}

// The side panel toggle lives at the toolbar's right edge; its label flips.
static void EnsureSidePanelVisible(ImGuiTestContext* ctx)
{
    if (ctx->ItemExists("**/Show Panel##tb"))
        ctx->ItemClick("**/Show Panel##tb");
    IM_CHECK_SILENT(ctx->ItemExists("**/Hide Panel##tb"));
}

static void RegisterGuiTests(ImGuiTestEngine* e)
{
    ImGuiTest* t = nullptr;

    // Diagnostic: dump live window names (run with --qa=dbg when a window
    // path in another test stops resolving).
    t = IM_REGISTER_TEST(e, "dbg", "windows");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        for (ImGuiWindow* w : ctx->UiContext->Windows)
            if (w->WasActive)
                ctx->LogInfo("window: '%s'", w->Name);
    };

    // Every side-panel page opens and shows a known control.
    t = IM_REGISTER_TEST(e, "gui", "side_panel_pages");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        // probe_window != null: ID-only ("##...") probes need an exact window
        // path — the engine's "**/" wildcard matches by visible label only.
        struct Page { const char* rail; const char* probe; const char* probe_window; };
        const Page pages[] = {
            { "Scope", "##scope_panel_run", "//Main Window/##sidepanel/Scope" },
            { "Signals", "**/Signal Generator 1", nullptr },
            { "PSU", "##voltage", "//Main Window/##sidepanel/PSU" },
            { "Meter", "**/Switch to multimeter mode", nullptr },
            { "Logic", "**/UART TX", nullptr },
            { "DAQ", "**/Record to File", nullptr },
            { "Analysis", "**/Spectrum Analyser", nullptr },
        };
        // Make sure the side panel is visible first (a rail click on the
        // already-active page collapses it).
        EnsureSidePanelVisible(ctx);
        for (const Page& page : pages)
        {
            // Via the View menu: a rail click on the already-active page
            // deliberately collapses the panel, which isn't wanted here.
            ctx->MenuClick(ImGuiTestRef(
                (std::string("View/Side Panel Page/") + page.rail).c_str()));
            ctx->Yield(2);
            if (page.probe_window != nullptr)
            {
                ImGuiTestItemInfo w = ctx->WindowInfo(page.probe_window);
                IM_CHECK(w.Window != nullptr);
                IM_CHECK(ctx->ItemExists(ctx->GetID(page.probe, w.Window->ID)));
            }
            else
            {
                IM_CHECK(ctx->ItemExists(page.probe));
            }
        }
        ctx->MenuClick("View/Side Panel Page/Scope"); // back to the default
    };

    // Toolbar run/stop button (reports its state via the Checked flag).
    t = IM_REGISTER_TEST(e, "gui", "run_stop");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        const bool was_running = ItemChecked(ctx, "**/###runstop");
        ctx->ItemClick("**/###runstop");
        ctx->Yield(2); // status flags update on the next submitted frame
        IM_CHECK(ItemChecked(ctx, "**/###runstop") == !was_running);
        ctx->ItemClick("**/###runstop");
        ctx->Yield(2);
        IM_CHECK(ItemChecked(ctx, "**/###runstop") == was_running);
    };

    // Toolbar cursor toggles.
    t = IM_REGISTER_TEST(e, "gui", "cursors");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        for (const char* which : { "**/Cursor 1##toolbar", "**/Cursor 2##toolbar" })
        {
            const bool was_on = ItemChecked(ctx, which);
            ctx->ItemClick(which);
            ctx->Yield(2);
            IM_CHECK(ItemChecked(ctx, which) == !was_on);
            ctx->ItemClick(which);
            ctx->Yield(2);
            IM_CHECK(ItemChecked(ctx, which) == was_on);
        }
    };

    // Hide/show the side panel from the toolbar (labels flip, IDs differ).
    t = IM_REGISTER_TEST(e, "gui", "panel_hide_show");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        EnsureSidePanelVisible(ctx);
        ctx->ItemClick("**/Hide Panel##tb");
        IM_CHECK(ctx->ItemExists("**/Show Panel##tb"));
        ctx->ItemClick("**/Show Panel##tb");
        IM_CHECK(ctx->ItemExists("**/Hide Panel##tb"));
    };

    // Cycle every theme (and a couple of text sizes) without crashing, and
    // land back on the defaults.
    t = IM_REGISTER_TEST(e, "gui", "themes_and_text_size");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        const char* themes[] = { "Classic Light", "Phosphor Retro",
            "Phosphor Modern", "Amber Retro", "Amber Modern", "Vector Retro",
            "Vector Modern", "Classic Dark" };
        for (const char* theme : themes)
        {
            ctx->MenuClick(
                ImGuiTestRef((std::string("View/Theme/") + theme).c_str()));
            // Theme switches can swap the font: park the mouse and let the
            // whole layout settle before the next hover chain starts.
            ParkMouse(ctx);
            ctx->Yield(8);
        }
        ctx->MenuClick("View/Text Size/Extra Large");
        ctx->Yield(3);
        ctx->MenuClick("View/Text Size/Normal");
        ctx->Yield(3);
    };

    // Inputs mode combo drives the device mode and the Meter page follows.
    t = IM_REGISTER_TEST(e, "gui", "inputs_mode_multimeter");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ImGuiTestItemInfo toolbar = ctx->WindowInfo("//Main Window/##toolbar");
        IM_CHECK(toolbar.Window != nullptr);
        ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
        ctx->ItemClick("//##Combo_00/Multimeter (CH1)");
        ctx->ItemClick("**/rail/Meter");
        ctx->Yield(2);
        ImGuiTestItemInfo meter = ctx->WindowInfo("//Main Window/##sidepanel/Meter");
        IM_CHECK(meter.Window != nullptr);
        IM_CHECK(ctx->ItemExists(ctx->GetID("##mm_mode", meter.Window->ID)));
        ctx->ItemClick("**/Leave multimeter mode");
        ctx->Yield(2);
        IM_CHECK(ctx->ItemExists("**/Switch to multimeter mode"));
        ctx->ItemClick("**/rail/Scope");
    };
}

// ---- Hardware loopback tests -------------------------------------------------
// Harness: SG1 wired to OSC1, SG2 wired to OSC2, board plugged in. Each test
// drives a 1 kHz sine on the signal generator and checks amplitude and
// frequency of the captured waveform. Skipped (warning) with no board.

static void HwLoopback(ImGuiTestContext* ctx, int channel)
{
    // The run starts immediately while the board is still enumerating —
    // give the connection a few real seconds before deciding to skip.
    for (int i = 0; i < 20; i++)
    {
        if (librador_is_connected() && librador_iso_thread_is_active())
            break;
        ctx->SleepNoSkip(0.25f, 0.25f);
    }
    if (!librador_is_connected() || !librador_iso_thread_is_active())
    {
        ctx->LogWarning("No Labrador board connected - skipping hardware test");
        return;
    }

    // Board must be in CH1+CH2 oscilloscope mode for both channels to sample
    SetMainRef(ctx);
    ImGuiTestItemInfo toolbar = ctx->WindowInfo("//Main Window/##toolbar");
    IM_CHECK(toolbar.Window != nullptr);
    ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
    ctx->ItemClick("//##Combo_00/CH1 + CH2 oscilloscope");
    ctx->SleepNoSkip(0.5f, 0.1f);

    // 100-sample sine at 10 us/sample = 1 kHz, 2 V amplitude, 0 V base
    unsigned char wave[100];
    for (int i = 0; i < 100; i++)
        wave[i] = (unsigned char)(127.5 + 127.5 * sin(2.0 * IM_PI * i / 100.0));
    IM_CHECK_GE(librador_update_signal_gen_settings(channel, wave, 100, 10.0, 2.0, 0.0), 0);
    ctx->SleepNoSkip(1.5f, 0.1f); // let the stream settle

    std::vector<double>* data = librador_get_analog_data(channel, 0.05, 5000, 0.0, 0);
    IM_CHECK(data != nullptr);
    IM_CHECK_GT((int)data->size(), 1000);

    double lo = 1e9, hi = -1e9;
    for (double v : *data)
    {
        lo = ImMin(lo, v);
        hi = ImMax(hi, v);
    }
    const double vpp = hi - lo;
    ctx->LogInfo("CH%d loopback: %.0f samples, Vpp = %.2f V", channel,
        (double)data->size(), vpp);
    // 2 V nominal; generous window for uncalibrated boards + probe losses
    IM_CHECK_GT(vpp, 1.2);
    IM_CHECK_LT(vpp, 3.0);

    // Frequency from mean-level crossings: expect ~1 kHz over the 50 ms window
    const double mid = (hi + lo) * 0.5;
    int crossings = 0;
    for (size_t i = 1; i < data->size(); i++)
        if (((*data)[i - 1] < mid) != ((*data)[i] < mid))
            crossings++;
    const double freq = crossings / 2.0 / 0.05;
    ctx->LogInfo("CH%d loopback: ~%.0f Hz", channel, freq);
    IM_CHECK_GT(freq, 800.0);
    IM_CHECK_LT(freq, 1200.0);

    // Park the generator at 0 V again
    unsigned char idle[16] = { 0 };
    librador_update_signal_gen_settings(channel, idle, 16, 10.0, 0.0, 0.0);
}

static void RegisterHwTests(ImGuiTestEngine* e)
{
    ImGuiTest* t = IM_REGISTER_TEST(e, "hw", "loopback_sg1_osc1");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwLoopback(ctx, 1); };
    t = IM_REGISTER_TEST(e, "hw", "loopback_sg2_osc2");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwLoopback(ctx, 2); };
}

// ---- Engine lifecycle ---------------------------------------------------------

void QaSetup(const char* run_filter)
{
    g_headless = (run_filter != nullptr);

    g_engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(g_engine);
    io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    io.ConfigNoThrottle = g_headless;
    io.ConfigLogToTTY = g_headless;

    RegisterGuiTests(g_engine);
    RegisterHwTests(g_engine);

    ImGuiTestEngine_Start(g_engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    if (g_headless)
        ImGuiTestEngine_QueueTests(g_engine, ImGuiTestGroup_Tests, run_filter,
            ImGuiTestRunFlags_RunFromCommandLine);
}

void QaDrawUI()
{
    if (g_engine != nullptr && !g_headless)
    {
        static bool open = true;
        ImGuiTestEngine_ShowTestEngineWindows(g_engine, &open);
    }
}

void QaPostSwap()
{
    if (g_engine != nullptr)
        ImGuiTestEngine_PostSwap(g_engine);
}

bool QaFinished()
{
    return g_engine != nullptr && ImGuiTestEngine_IsTestQueueEmpty(g_engine);
}

int QaReportAndExitCode()
{
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(g_engine, &summary);
    fprintf(stderr, "QA: %d/%d tests passed (%d in queue at exit)\n",
        summary.CountSuccess, summary.CountTested, summary.CountInQueue);
    return (summary.CountSuccess == summary.CountTested) ? 0 : 1;
}

void QaShutdown()
{
    if (g_engine == nullptr)
        return;
    ImGuiTestEngine_Stop(g_engine);
    // The engine context must outlive the ImGui context (destroyed in
    // ~AppBase), so it is deliberately leaked — the process is exiting.
    g_engine = nullptr;
}

#endif // LABRADOR_QA

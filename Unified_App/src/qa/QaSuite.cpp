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

// Prediction-QA: a test sets the pending capture path; the render loop dumps
// the next frame to it and clears it (single-shot). Same thread hand-off model
// as the rest of the engine's coroutine/main-loop interplay.
static std::string g_capture_path;
void QaRequestFrameDump(const char* path) { g_capture_path = path ? path : ""; }
const char* QaConsumeFrameDump()
{
    if (g_capture_path.empty())
        return nullptr;
    static std::string taken;
    taken.swap(g_capture_path);
    g_capture_path.clear();
    return taken.c_str();
}

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

// ---- Interaction fuzz suite ---------------------------------------------------
// Crash-hunting tests: walk/mash every reachable interaction surface and rely
// on the crash handler to catch anything that dies. Two categories:
//   "fuzz"  (--qa=fuzz)  safe to run unattended with a board attached
//   "fuzzx" (--qa=<name>) run individually and deliberately: they quit the
//           app, wedge the headless harness, or rewrite board calibration.
// Neither runs under plain --qa (see QaSetup: "all" maps to gui+hw).
// Blacklist: quit/reflash (hardware safety), native file dialogs (cannot be
// driven headlessly), external URLs.

#include "SDL3/SDL.h"

static bool FuzzBlacklisted(const char* label)
{
    static const char* bad[] = { "Quit", "Reflash", "CSV", "recording",
        "Replay", "Reset USB", "espotek", "GitHub", "Export", "Load",
        "Browse", "Start" };
    if (label == nullptr)
        return false;
    for (const char* b : bad)
        if (ImStristr(label, nullptr, b, nullptr) != nullptr)
            return true;
    return false;
}

// Click every gathered item under `parent` (skipping blacklisted labels),
// closing any popup each click opens.
static void FuzzClickAllUnder(ImGuiTestContext* ctx, ImGuiTestRef parent, int depth)
{
    ctx->PopupCloseAll();
    ImGuiTestItemList items;
    ctx->GatherItems(&items, parent, depth);
    ctx->LogInfo("fuzz: gathered %d items", items.GetSize());
    if (items.GetSize() == 0)
        for (ImGuiWindow* w : ctx->UiContext->Windows)
            if (w->WasActive)
                ctx->LogInfo("fuzz: live window: '%s'", w->Name);
    for (int i = 0; i < items.GetSize(); i++)
    {
        const ImGuiTestItemInfo& item = *items[i];
        if (FuzzBlacklisted(item.DebugLabel))
            continue;
        if (ctx->ItemInfo(item.ID, ImGuiTestOpFlags_NoError).ID == 0)
            continue; // item vanished (page changed under us)
        ctx->LogInfo("fuzz: click #%d '%s'", i, item.DebugLabel);
        ctx->MouseSetViewport(item.Window);
        ctx->ItemClick(item.ID);
        ctx->PopupCloseAll();
        ctx->Yield(2);
        if (ctx->IsError())
            ctx->TestOutput->Status = ImGuiTestStatus_Running; // keep walking; only crashes matter here
    }
}

static void RegisterFuzzTests(ImGuiTestEngine* e)
{
    ImGuiTest* t = nullptr;

    // Click every safe menu item (incl. toggles twice).
    t = IM_REGISTER_TEST(e, "fuzz", "menu_walk");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        const char* paths[] = {
            "Device/Calibration...",
            "Scope/Run", "Scope/Run",
            "Scope/Auto-fit both axes",
            "Scope/Hardware Gain/Auto", "Scope/Hardware Gain/Auto",
            "Scope/XY Mode", "Scope/XY Mode",
            "Scope/Eye Diagram", "Scope/Eye Diagram",
            "Scope/Cursor 1", "Scope/Cursor 2",
            "Scope/Signal Properties",
            "Tools/Spectrum Analyser", "Tools/Spectrum Analyser",
            "Tools/Network Analyser", "Tools/Network Analyser",
            "Tools/Multimeter", "Tools/Multimeter",
            "Tools/Logic Analyzer", "Tools/Logic Analyzer",
            "Tools/DAQ Recorder...",
            "View/Side Panel", "View/Side Panel",
            "View/CRT Scanlines", "View/CRT Scanlines",
            "View/Debug/Debug console",
            "Help/User Guide",
            "Help/Keyboard Shortcuts",
            "Help/Pinout Diagram",
            "Help/Troubleshooting",
            "Help/About EspoTek Labrador",
        };
        for (const char* p : paths)
        {
            ctx->LogInfo("fuzz: menu '%s'", p);
            ctx->MenuClick(p);
            ctx->Yield(3);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
            SetMainRef(ctx);
        }
    };

    // Keyboard mash (no Escape: resets USB with a board attached).
    t = IM_REGISTER_TEST(e, "fuzz", "keyboard_mash");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ParkMouse(ctx);
        const ImGuiKeyChord keys[] = {
            ImGuiKey_Space, ImGuiKey_F, ImGuiKey_B, ImGuiKey_1, ImGuiKey_2,
            ImGuiKey_F1, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
            ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Tab,
            ImGuiKey_Enter, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_PageUp,
            ImGuiKey_PageDown, ImGuiKey_Delete, ImGuiKey_Backspace,
            ImGuiKey_A, ImGuiKey_C, ImGuiKey_D, ImGuiKey_G, ImGuiKey_M,
            ImGuiKey_P, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T, ImGuiKey_X,
            ImGuiKey_Z, ImGuiKey_0, ImGuiKey_9, ImGuiKey_F2, ImGuiKey_F5,
            ImGuiKey_F12,
            ImGuiMod_Ctrl | ImGuiKey_A, ImGuiMod_Ctrl | ImGuiKey_C,
            ImGuiMod_Ctrl | ImGuiKey_V, ImGuiMod_Ctrl | ImGuiKey_Z,
            ImGuiMod_Shift | ImGuiKey_LeftArrow,
        };
        for (int round = 0; round < 2; round++) // running, then paused
        {
            for (ImGuiKeyChord k : keys)
            {
                ctx->KeyPress(k);
                ctx->Yield(2);
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
            ctx->KeyPress(ImGuiKey_Space); // flip run/pause for round 2
            ctx->Yield(4);
        }
        ctx->KeyPress(ImGuiKey_Space);
        ctx->Yield(2);
    };

    // Plot abuse: zoom extremes, pans (incl. paused scrollback), context
    // menu, box-zoom drags, double-click autofit.
    t = IM_REGISTER_TEST(e, "fuzz", "plot_abuse");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ImGuiWindow* window = ctx->GetWindowByRef("//Main Window");
        IM_CHECK(window != nullptr);
        const ImRect r = window->Rect();
        const ImVec2 c(r.Min.x + r.GetWidth() * 0.4f, r.Min.y + r.GetHeight() * 0.5f);
        for (int round = 0; round < 2; round++) // running, then paused
        {
            ctx->MouseMoveToPos(c);
            for (int i = 0; i < 15; i++) ctx->MouseWheelY(+5.0f);   // zoom way in
            for (int i = 0; i < 30; i++) ctx->MouseWheelY(-5.0f);   // zoom way out
            ctx->MouseDoubleClick(0);
            ctx->Yield(2);
            // pans, including far into the past
            for (int i = 0; i < 6; i++)
            {
                ctx->MouseMoveToPos(ImVec2(c.x - 200, c.y));
                ctx->MouseDown(0);
                ctx->MouseMoveToPos(ImVec2(c.x + 300, c.y + 40));
                ctx->MouseUp(0);
                ctx->Yield(1);
            }
            // right-drag box zoom, tiny and huge
            ctx->MouseMoveToPos(c);
            ctx->MouseDown(1);
            ctx->MouseMoveToPos(ImVec2(c.x + 2, c.y + 2));
            ctx->MouseUp(1);
            ctx->Yield(2);
            ctx->MouseDown(1);
            ctx->MouseMoveToPos(ImVec2(c.x - 400, c.y - 200));
            ctx->MouseUp(1);
            ctx->Yield(2);
            // context menu open/close
            ctx->MouseClick(1);
            ctx->Yield(3);
            ctx->PopupCloseAll();
            ctx->MouseDoubleClick(0);
            ctx->Yield(2);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
            ctx->KeyPress(ImGuiKey_Space); // pause for round 2
            ctx->Yield(4);
        }
        ctx->KeyPress(ImGuiKey_Space);
        ctx->Yield(2);
    };

    // Click every widget on every side-panel page.
    t = IM_REGISTER_TEST(e, "fuzz", "page_widget_walk");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        EnsureSidePanelVisible(ctx);
        const char* pages[] = { "Scope", "Signals", "PSU", "Meter", "Logic",
            "DAQ", "Analysis" };
        for (const char* page : pages)
        {
            SetMainRef(ctx);
            ctx->MenuClick(ImGuiTestRef(
                (std::string("View/Side Panel Page/") + page).c_str()));
            ctx->Yield(3);
            ImGuiTestItemInfo w = ctx->WindowInfo(
                (std::string("//Main Window/##sidepanel/") + page).c_str(),
                ImGuiTestOpFlags_NoError);
            if (w.Window == nullptr)
            {
                ctx->LogInfo("fuzz: page '%s' window not found, skipping", page);
                continue;
            }
            ctx->LogInfo("fuzz: === page '%s' ===", page);
            FuzzClickAllUnder(ctx, w.Window->ID, 4);
        }
        SetMainRef(ctx);
        ctx->MenuClick("View/Side Panel Page/Scope");
    };

    // Drag every draggable thing to extremes on the value-heavy pages.
    t = IM_REGISTER_TEST(e, "fuzz", "drag_extremes");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        EnsureSidePanelVisible(ctx);
        const char* pages[] = { "Scope", "Signals", "PSU", "Logic" };
        for (const char* page : pages)
        {
            SetMainRef(ctx);
            ctx->MenuClick(ImGuiTestRef(
                (std::string("View/Side Panel Page/") + page).c_str()));
            ctx->Yield(3);
            ImGuiTestItemInfo w = ctx->WindowInfo(
                (std::string("//Main Window/##sidepanel/") + page).c_str(),
                ImGuiTestOpFlags_NoError);
            if (w.Window == nullptr)
                continue;
            ImGuiTestItemList items;
            ctx->GatherItems(&items, w.Window->ID, 4);
            ctx->LogInfo("fuzz: === drag page '%s', %d items ===", page,
                items.GetSize());
            for (int i = 0; i < items.GetSize(); i++)
            {
                const ImGuiTestItemInfo& item = *items[i];
                if (FuzzBlacklisted(item.DebugLabel))
                    continue;
                if (ctx->ItemInfo(item.ID, ImGuiTestOpFlags_NoError).ID == 0)
                    continue;
                ctx->LogInfo("fuzz: drag #%d '%s'", i, item.DebugLabel);
                ctx->ItemDragWithDelta(item.ID, ImVec2(+700, 0));
                ctx->Yield(1);
                ctx->ItemDragWithDelta(item.ID, ImVec2(-1400, 0));
                ctx->Yield(1);
                ctx->PopupCloseAll();
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
        }
        SetMainRef(ctx);
        ctx->MenuClick("View/Side Panel Page/Scope");
    };

    // Every device mode from the toolbar combo, twice around.
    t = IM_REGISTER_TEST(e, "fuzz", "mode_cycle");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ImGuiTestItemInfo toolbar = ctx->WindowInfo("//Main Window/##toolbar");
        IM_CHECK(toolbar.Window != nullptr);
        for (int round = 0; round < 2; round++)
        {
            for (int i = 0; i < 7; i++) // InputsControl::ModeCount
            {
                ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
                ctx->Yield(2);
                ImGuiTestItemList items;
                ctx->GatherItems(&items, "//##Combo_00", 1);
                if (i < items.GetSize())
                {
                    ctx->LogInfo("fuzz: mode -> '%s'", items[i]->DebugLabel);
                    ctx->ItemClick(items[i]->ID);
                }
                else
                    ctx->PopupCloseAll();
                ctx->Yield(4);
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
        }
        // back to default two-channel scope mode
        ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
        ctx->ItemClick("//##Combo_00/CH1 + CH2 oscilloscope");
        ctx->Yield(4);
    };

    // OS window resize: degenerate and huge sizes.
    t = IM_REGISTER_TEST(e, "fuzz", "window_resize");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        int nwin = 0;
        SDL_Window** wins = SDL_GetWindows(&nwin);
        SDL_Window* win = (nwin > 0) ? wins[0] : nullptr;
        SDL_free(wins);
        IM_CHECK(win != nullptr);
        int w0 = 0, h0 = 0;
        SDL_GetWindowSize(win, &w0, &h0);
        const int sizes[][2] = { { 320, 200 }, { 50, 50 }, { 1, 1 },
            { 8000, 8000 }, { 200, 2000 }, { 2000, 100 }, { w0, h0 } };
        for (auto& s : sizes)
        {
            ctx->LogInfo("fuzz: resize %dx%d", s[0], s[1]);
            // AppKit window ops must happen on the main thread; tests run on
            // the engine's coroutine thread. Async so the main loop (blocked
            // on this coroutine right now) can service it on the next frame.
            struct SizeReq { SDL_Window* w; int x, y; };
            SizeReq* req = new SizeReq{ win, s[0], s[1] };
            SDL_RunOnMainThread(
                [](void* ud) {
                    SizeReq* r = (SizeReq*)ud;
                    SDL_SetWindowSize(r->w, r->x, r->y);
                    delete r;
                },
                req, false);
            ctx->Yield(8);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
    };

    // Seeded monkey: random clicks/keys/wheel over the whole main window.
    t = IM_REGISTER_TEST(e, "fuzz", "monkey");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        unsigned int rng = 0xC0FFEE42u;
        auto next = [&rng]() {
            rng = rng * 1664525u + 1013904223u;
            return rng >> 8;
        };
        const ImGuiKeyChord keys[] = { ImGuiKey_Space, ImGuiKey_F, ImGuiKey_B,
            ImGuiKey_1, ImGuiKey_2, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
            ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Tab, ImGuiKey_Enter };
        ImGuiTestItemList items;
        for (int i = 0; i < 400; i++)
        {
            if ((i % 25) == 0)
            {
                ctx->PopupCloseAll();
                ctx->GatherItems(&items, "//Main Window", 5);
                ctx->LogInfo("fuzz: monkey iter %d, %d items", i, items.GetSize());
            }
            const unsigned int action = next() % 6;
            if (action <= 2 && items.GetSize() > 0)
            {
                const ImGuiTestItemInfo& item = *items[next() % items.GetSize()];
                if (FuzzBlacklisted(item.DebugLabel))
                    continue;
                if (ctx->ItemInfo(item.ID, ImGuiTestOpFlags_NoError).ID == 0)
                    continue;
                ctx->LogInfo("fuzz: monkey click '%s'", item.DebugLabel);
                ctx->ItemClick(item.ID);
                ctx->PopupCloseAll();
            }
            else if (action == 3)
            {
                ctx->PopupCloseAll(); // no Enter into an open menu
                ctx->KeyPress(keys[next() % IM_ARRAYSIZE(keys)]);
            }
            else
            {
                ImGuiWindow* window = ctx->GetWindowByRef("//Main Window");
                if (window == nullptr)
                    continue;
                const ImRect r = window->Rect();
                ctx->MouseMoveToPos(ImVec2(
                    r.Min.x + (next() % (int)r.GetWidth()),
                    r.Min.y + (next() % (int)r.GetHeight())));
                if (action == 4)
                    ctx->MouseWheelY((float)(int)(next() % 21) - 10.0f);
                else
                    ctx->MouseClick(next() % 2);
                ctx->PopupCloseAll();
            }
            ctx->Yield(1);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
        SetMainRef(ctx);
    };

    // Calibration wizard end-to-end (scope path). REWRITES the attached
    // board's calibration values in settings.ini — run deliberately.
    t = IM_REGISTER_TEST(e, "fuzzx", "calibration_wizard");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ctx->MenuClick("Device/Calibration...");
        ctx->Yield(3);
        ctx->SetRef("Calibration");
        if (ctx->ItemExists("Calibrate Oscilloscope##cal_start_scope"))
        {
            ctx->ItemClick("Calibrate Oscilloscope##cal_start_scope");
            ctx->Yield(3);
            for (int i = 0; i < 60; i++) // step through, bounded
            {
                if (ctx->GetWindowByRef("//Calibration") == nullptr)
                    break;
                if (ctx->ItemExists("Next##cal_next"))
                    ctx->ItemClick("Next##cal_next");
                else if (ctx->ItemExists("Done##cal_scope_done"))
                {
                    ctx->ItemClick("Done##cal_scope_done");
                    break;
                }
                else if (ctx->ItemExists("OK##cal_failed_ok"))
                {
                    ctx->ItemClick("OK##cal_failed_ok");
                    break;
                }
                ctx->SleepNoSkip(1.0f, 0.5f); // measuring phases take real time
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
        }
        // close the window if it is still up
        SetMainRef(ctx);
    };

    // Minimize / restore / fullscreen. CAVEAT: the main loop stops pumping
    // while minimized, so the queued restore never runs and the headless
    // harness wedges — run manually, be ready to un-minimize by hand.
    t = IM_REGISTER_TEST(e, "fuzzx", "minimize_restore");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        int nwin = 0;
        SDL_Window** wins = SDL_GetWindows(&nwin);
        SDL_Window* win = (nwin > 0) ? wins[0] : nullptr;
        SDL_free(wins);
        IM_CHECK(win != nullptr);
        enum Op { OpMin, OpRestore, OpMax, OpFsOn, OpFsOff };
        const Op ops[] = { OpMin, OpRestore, OpMax, OpRestore, OpFsOn,
            OpFsOff, OpMin, OpRestore };
        for (Op op : ops)
        {
            ctx->LogInfo("fuzz: window op %d", (int)op);
            struct Req { SDL_Window* w; Op op; };
            Req* req = new Req{ win, op };
            SDL_RunOnMainThread(
                [](void* ud) {
                    Req* r = (Req*)ud;
                    switch (r->op)
                    {
                    case OpMin:     SDL_MinimizeWindow(r->w); break;
                    case OpRestore: SDL_RestoreWindow(r->w); break;
                    case OpMax:     SDL_MaximizeWindow(r->w); break;
                    case OpFsOn:    SDL_SetWindowFullscreen(r->w, true); break;
                    case OpFsOff:   SDL_SetWindowFullscreen(r->w, false); break;
                    }
                    delete r;
                },
                req, false);
            ctx->Yield(30); // let it render a while in that state
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
    };

    // Reset the USB connection (the Esc shortcut path) while streaming,
    // repeatedly, and once while paused, waiting out each reconnect.
    t = IM_REGISTER_TEST(e, "fuzz", "reset_usb");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        for (int round = 0; round < 3; round++)
        {
            if (round == 2)
            {
                ctx->KeyPress(ImGuiKey_Space); // paused this time
                ctx->Yield(4);
            }
            ctx->LogInfo("fuzz: reset usb, round %d", round);
            ctx->MenuClick("Device/Reset USB connection");
            ctx->SleepNoSkip(4.0f, 0.5f); // reconnect + auto-restream
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
        ctx->KeyPress(ImGuiKey_Space); // resume if still paused
        ctx->Yield(4);
    };

    // Quit through the app's own File menu while connected and streaming —
    // exercises the real shutdown path (thread joins, USB teardown). The app
    // exits mid-test, killing the run: run this one alone.
    t = IM_REGISTER_TEST(e, "fuzzx", "quit_while_connected");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ctx->SleepNoSkip(3.0f, 0.5f); // make sure streaming is up
        ctx->MenuClick("File/Quit");
        ctx->SleepNoSkip(10.0f, 1.0f); // should be dead before this ends
    };

    // Interactive walk INSIDE the mobile and compact layouts (the
    // desktop-forced QA suite never exercises them): switch live, click
    // every safe item in every window, then switch on.
    t = IM_REGISTER_TEST(e, "fuzz", "layout_walk");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        const char* layouts[] = { "Mobile", "Compact (800x480)", "Desktop" };
        for (const char* layout : layouts)
        {
            // Switch via the View menu if this layout still has one.
            if (ctx->GetWindowByRef("//Main Window") != nullptr)
            {
                ctx->SetRef("Main Window");
                ctx->MenuClick(ImGuiTestRef(
                    (std::string("View/Layout/") + layout).c_str()));
                ctx->Yield(10);
            }
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
            ctx->LogInfo("fuzz: === walking layout %s ===", layout);
            if (strcmp(layout, "Desktop") == 0)
                break; // back home; the desktop walk already ran elsewhere
            ImGuiWindow* main = ctx->GetWindowByRef("//Main Window");
            if (main == nullptr)
                continue;
            ctx->SetRef(main->ID);
            // Visit every page via its nav-rail label, then click everything
            // the page shows.
            const char* rail_pages[] = { "Scope", "Signals", "PSU", "Meter",
                "Logic", "DAQ", "Analysis" };
            for (const char* page : rail_pages)
            {
                ctx->PopupCloseAll();
                ctx->MenuClick(ImGuiTestRef(
                    (std::string("View/Side Panel Page/") + page).c_str()));
                ctx->Yield(4);
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
                ctx->LogInfo("fuzz: --- %s page '%s' ---", layout, page);
                FuzzClickAllUnder(ctx, main->ID, 5);
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
        }
    };

    // Live layout switches (desktop -> compact -> mobile -> desktop).
    t = IM_REGISTER_TEST(e, "fuzz", "layout_switch");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        const char* layouts[] = { "Compact (800x480)", "Mobile", "Desktop" };
        for (const char* layout : layouts)
        {
            ctx->LogInfo("fuzz: layout -> %s", layout);
            if (ctx->GetWindowByRef("//Main Window") != nullptr)
            {
                ctx->SetRef("Main Window");
                ctx->MenuClick(ImGuiTestRef(
                    (std::string("View/Layout/") + layout).c_str()));
            }
            ctx->Yield(10);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
    };

    // Esc is the reset-USB shortcut, and users spam it. Rapid resets land
    // while the previous reconnect is still in flight; the reconnect state
    // machine must survive that. (Repro for a field crash: 5-10 quick Escs.)
    t = IM_REGISTER_TEST(e, "fuzz", "esc_spam");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        // The Esc handler is gated on librador_is_connected(), so a fixed
        // cadence mostly no-ops while disconnected. Fire the instant the
        // connection comes (back) up so the reset lands inside connection
        // setup, like a user hammering the key.
        for (int i = 0; i < 12; i++)
        {
            for (int w = 0; w < 100 && !librador_is_connected(); w++)
                ctx->SleepNoSkip(0.1f, 0.1f);
            ctx->LogInfo("fuzz: Esc press %d (connected=%d)", i,
                (int)librador_is_connected());
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(2);
            ctx->KeyPress(ImGuiKey_Escape); // double-tap for good measure
            ctx->Yield(2);
            if (ctx->IsError())
                ctx->TestOutput->Status = ImGuiTestStatus_Running;
        }
        ctx->SleepNoSkip(8.0f, 1.0f); // let the final reconnect settle
        ctx->Yield(4);
    };

    // Text-entry fuzz. Activating ANY text field routes through the SDL3 IME
    // path (ImGui_ImplSDL3_UpdateIme), which crashed on desktop when the OS
    // gave the window keyboard focus (null io.UserData deref). Also feeds
    // garbage into the numeric parsers. Run focused to exercise the IME path
    // (headless has no keyboard focus, so SDL_GetKeyboardFocus() is null and
    // the deref is skipped — see the focused-run note in the QA skill).
    t = IM_REGISTER_TEST(e, "fuzz", "text_edit");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        const char* junk[] = { "abc", "1e9", "-999999", "nan", "inf",
            "!@#$%^&*()", "", "999999999999999999999", "0x10", ".." };

        // 1. Help search box (a plain InputText).
        ctx->MenuClick("Help/User Guide");
        ctx->Yield(4);
        if (ctx->GetWindowByRef("//Labrador User Guide") != nullptr)
        {
            ctx->SetRef("Labrador User Guide");
            if (ctx->ItemExists("##help_search"))
                for (const char* s : junk)
                {
                    ctx->ItemClick("##help_search");
                    ctx->Yield(2);
                    ctx->KeyChars(s);
                    ctx->Yield(2);
                    ctx->KeyPress(ImGuiKey_Enter);
                    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
                    ctx->KeyPress(ImGuiKey_Delete);
                    if (ctx->IsError())
                        ctx->TestOutput->Status = ImGuiTestStatus_Running;
                }
        }
        SetMainRef(ctx);

        // 2. Numeric fields on the Signals page: double-click a DragFloat to
        // type into it, then feed garbage values.
        EnsureSidePanelVisible(ctx);
        ctx->MenuClick("View/Side Panel Page/Signals");
        ctx->Yield(3);
        ImGuiTestItemInfo w = ctx->WindowInfo(
            "//Main Window/##sidepanel/Signals", ImGuiTestOpFlags_NoError);
        if (w.Window != nullptr)
        {
            ImGuiTestItemList items;
            ctx->GatherItems(&items, w.Window->ID, 5);
            int typed = 0;
            for (int i = 0; i < items.GetSize() && typed < 14; i++)
            {
                const ImGuiTestItemInfo& it = *items[i];
                if (FuzzBlacklisted(it.DebugLabel))
                    continue;
                if (ctx->ItemInfo(it.ID, ImGuiTestOpFlags_NoError).ID == 0)
                    continue;
                ctx->MouseSetViewport(it.Window);
                ctx->ItemDoubleClick(it.ID);
                ctx->Yield(2);
                ctx->KeyChars(junk[typed % IM_ARRAYSIZE(junk)]);
                ctx->Yield(2);
                ctx->KeyPress(ImGuiKey_Enter);
                ctx->Yield(2);
                ctx->PopupCloseAll();
                typed++;
                if (ctx->IsError())
                    ctx->TestOutput->Status = ImGuiTestStatus_Running;
            }
        }
        SetMainRef(ctx);
    };
}

// ---- Prediction QA scenarios -------------------------------------------------
// A scenario reaches a state, captures a "before" frame, performs one named
// interaction, and captures an "after" frame. A human/model then predicts what
// the interaction should do from the before frame and checks the after frame
// against that prediction (see tools/qa_predict_report.py). Capture dir comes
// from LABRADOR_QA_CAPTURE_DIR (default /tmp/labqa_predict). Run with
// --qa=predict; the group is opt-in like fuzz.

static void QaCapture(ImGuiTestContext* ctx, const char* name, const char* which)
{
    const char* dir = SDL_getenv("LABRADOR_QA_CAPTURE_DIR");
    if (dir == nullptr)
        dir = "/tmp/labqa_predict";
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%s.ppm", dir, name, which);
    ctx->Yield(2); // let the UI settle before grabbing the frame
    QaRequestFrameDump(path);
    ctx->Yield(3); // the render loop dumps on one of these frames
    ctx->LogInfo("predict: captured %s", path);
}

static void RegisterPredictScenarios(ImGuiTestEngine* e)
{
    ImGuiTest* t = nullptr;

    // 1. Auto Fit then zoom: the manual zoom must persist. Before this fix the
    //    view re-fit every frame with the panel hidden, so the zoom snapped
    //    back — the "after" frame would look identical to "before".
    t = IM_REGISTER_TEST(e, "predict", "autofit_zoom_persists");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ctx->MenuClick("View/Side Panel Page/Scope");
        if (ctx->ItemExists("**/Hide Panel##tb"))
            ctx->ItemClick("**/Hide Panel##tb");
        ctx->Yield(4);
        ctx->ItemClick("**/Auto Fit##toolbar"); // start from a fitted view
        ctx->Yield(4);
        QaCapture(ctx, "autofit_zoom_persists", "before");
        // interaction: zoom the plot in with the wheel, several notches
        ImGuiWindow* w = ctx->GetWindowByRef("//Main Window");
        if (w != nullptr)
        {
            const ImRect r = w->Rect();
            ctx->MouseMoveToPos(ImVec2(r.Min.x + r.GetWidth() * 0.30f,
                r.Min.y + r.GetHeight() * 0.40f));
            for (int i = 0; i < 8; i++)
                ctx->MouseWheelY(2.0f);
        }
        ctx->Yield(8); // a stuck auto-fit would undo the zoom within a frame
        QaCapture(ctx, "autofit_zoom_persists", "after");
    };

    // 2. Theme switch: classic-dark -> amber-retro.
    t = IM_REGISTER_TEST(e, "predict", "theme_switch");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        ctx->MenuClick("View/Theme/Classic Dark");
        ParkMouse(ctx);
        ctx->Yield(8);
        QaCapture(ctx, "theme_switch", "before");
        ctx->MenuClick("View/Theme/Amber Retro");
        ParkMouse(ctx);
        ctx->Yield(8);
        QaCapture(ctx, "theme_switch", "after");
    };

    // 3. Side-panel page navigation: Scope -> Signals.
    t = IM_REGISTER_TEST(e, "predict", "page_nav_signals");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        SetMainRef(ctx);
        EnsureSidePanelVisible(ctx);
        ctx->MenuClick("View/Side Panel Page/Scope");
        ctx->Yield(4);
        QaCapture(ctx, "page_nav_signals", "before");
        ctx->MenuClick("View/Side Panel Page/Signals");
        ctx->Yield(4);
        QaCapture(ctx, "page_nav_signals", "after");
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

// Run/Stop must freeze the capture record device-side (librador snapshot)
// so the paused view can be inspected in full detail, and reads with a
// delay must reach seconds-old history — the Qt app's pause behaviour.
static void HwPauseInspect(ImGuiTestContext* ctx)
{
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

    SetMainRef(ctx);
    ImGuiTestItemInfo toolbar = ctx->WindowInfo("//Main Window/##toolbar");
    IM_CHECK(toolbar.Window != nullptr);
    ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
    ctx->ItemClick("//##Combo_00/CH1 + CH2 oscilloscope");
    ctx->SleepNoSkip(0.5f, 0.1f);
    if (librador_get_paused(1)) // a previous failed run may have left it paused
    {
        ctx->ItemClick("**/###runstop");
        ctx->Yield(3);
    }
    IM_CHECK(!librador_get_paused(1));

    // 1 kHz sine on SG1, then let several seconds of it into the record so
    // the delayed read below lands inside the sine region
    unsigned char wave[100];
    for (int i = 0; i < 100; i++)
        wave[i] = (unsigned char)(127.5 + 127.5 * sin(2.0 * IM_PI * i / 100.0));
    IM_CHECK_GE(librador_update_signal_gen_settings(1, wave, 100, 10.0, 2.0, 0.0), 0);
    ctx->SleepNoSkip(4.0f, 0.1f);

    // Pause through the toolbar; PlotWidget syncs the device-side pause
    ctx->ItemClick("**/###runstop");
    ctx->Yield(3);
    IM_CHECK(librador_get_paused(1));

    std::vector<double>* fetched = librador_get_analog_data(1, 0.05, 5000, 0.0, 0);
    IM_CHECK(fetched != nullptr);
    const std::vector<double> frozen = *fetched;
    IM_CHECK_GT((int)frozen.size(), 1000);

    // Record must not move while paused, even though the input keeps
    // changing: park the generator, wait, and re-read
    unsigned char idle[16] = { 0 };
    librador_update_signal_gen_settings(1, idle, 16, 10.0, 0.0, 0.0);
    ctx->SleepNoSkip(1.5f, 0.1f);
    fetched = librador_get_analog_data(1, 0.05, 5000, 0.0, 0);
    IM_CHECK(fetched != nullptr);
    IM_CHECK(frozen == *fetched);

    // Scrollback: a read 3 s into the frozen record still shows the sine
    fetched = librador_get_analog_data(1, 0.05, 5000, 3.0, 0);
    IM_CHECK(fetched != nullptr);
    double lo = 1e9, hi = -1e9;
    for (double v : *fetched)
    {
        lo = ImMin(lo, v);
        hi = ImMax(hi, v);
    }
    ctx->LogInfo("paused record at -3 s: Vpp = %.2f V", hi - lo);
    IM_CHECK_GT(hi - lo, 1.2);
    IM_CHECK_LT(hi - lo, 3.0);

    // Unpause: capture resumes and now sees the parked (flat) generator
    ctx->ItemClick("**/###runstop");
    ctx->Yield(3);
    IM_CHECK(!librador_get_paused(1));
    ctx->SleepNoSkip(1.5f, 0.1f);
    fetched = librador_get_analog_data(1, 0.05, 5000, 0.0, 0);
    IM_CHECK(fetched != nullptr);
    lo = 1e9; hi = -1e9;
    for (double v : *fetched)
    {
        lo = ImMin(lo, v);
        hi = ImMax(hi, v);
    }
    ctx->LogInfo("resumed capture: Vpp = %.2f V", hi - lo);
    IM_CHECK_LT(hi - lo, 0.8);
}

// A USB reset must not silence the signal generators: onDeviceConnected
// re-pushes the widgets' current settings (SGControl::markDirty), so a
// generator that was running keeps running after the link comes back.
// Regression test for reconnect going dark (reset() used to send turnOff).
static void HwReconnectResendsSg2(ImGuiTestContext* ctx)
{
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

    // Both channels sampling so OSC2 sees the SG2 loopback
    SetMainRef(ctx);
    ImGuiTestItemInfo toolbar = ctx->WindowInfo("//Main Window/##toolbar");
    IM_CHECK(toolbar.Window != nullptr);
    ctx->ItemClick(ctx->GetID("##toolbar_mode", toolbar.Window->ID));
    ctx->ItemClick("//##Combo_00/CH1 + CH2 oscilloscope");
    ctx->SleepNoSkip(0.5f, 0.1f);

    // Drive SG2 through the UI like a user: Signals page, power toggle ON.
    EnsureSidePanelVisible(ctx);
    ctx->MenuClick("View/Side Panel Page/Signals");
    ctx->Yield(3);
    ImGuiTestItemInfo signals_w = ctx->WindowInfo("//Main Window/##sidepanel/Signals");
    IM_CHECK(signals_w.Window != nullptr);
    const ImGuiID toggle_id
        = ctx->GetID("sg2/Signal Generator 2 (SG2)_toggle", signals_w.Window->ID);
    if (ctx->ItemInfo(toggle_id, ImGuiTestOpFlags_NoError).ID == 0)
    {
        // Collapsible section may be closed from a previous session
        ctx->ItemClick("**/Signal Generator 2");
        ctx->Yield(2);
    }
    auto toggle_on = [&]() {
        ImGuiTestItemInfo info = ctx->ItemInfo(toggle_id);
        return (info.ID != 0) && (info.StatusFlags & ImGuiItemStatusFlags_Checked) != 0;
    };
    if (!toggle_on())
    {
        ctx->ItemClick(toggle_id);
        ctx->Yield(2);
    }
    IM_CHECK(toggle_on());
    ctx->SleepNoSkip(1.5f, 0.1f); // default 1 kHz sine onto the loopback wire

    auto osc2_vpp = [&]() -> double {
        std::vector<double>* d = librador_get_analog_data(2, 0.05, 5000, 0.0, 0);
        if (d == nullptr || d->empty())
            return 0.0;
        double lo = 1e9, hi = -1e9;
        for (double v : *d)
        {
            lo = ImMin(lo, v);
            hi = ImMax(hi, v);
        }
        return hi - lo;
    };

    const double vpp_before = osc2_vpp();
    ctx->LogInfo("SG2/OSC2 before reset: Vpp = %.2f V", vpp_before);
    IM_CHECK_GT(vpp_before, 0.3); // the widget-driven signal must be live

    // Host-side USB reset (the Esc shortcut path). pollDevice reconnects and
    // onDeviceConnected must re-push the generator settings by itself — no
    // UI interaction below this line until the post-reconnect capture.
    librador_reset_usb();
    for (int i = 0; i < 40; i++)
    {
        if (librador_is_connected() && librador_iso_thread_is_active())
            break;
        ctx->SleepNoSkip(0.25f, 0.25f);
    }
    IM_CHECK(librador_is_connected() && librador_iso_thread_is_active());
    ctx->SleepNoSkip(1.5f, 0.1f); // fresh samples of the (resent) waveform

    const double vpp_after = osc2_vpp();
    ctx->LogInfo("SG2/OSC2 after reconnect: Vpp = %.2f V", vpp_after);
    IM_CHECK_GT(vpp_after, 0.3); // fails when the reconnect turned SG2 off

    // Leave the board quiet
    ctx->ItemClick(toggle_id);
    ctx->Yield(2);
}

static void RegisterHwTests(ImGuiTestEngine* e)
{
    ImGuiTest* t = IM_REGISTER_TEST(e, "hw", "loopback_sg1_osc1");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwLoopback(ctx, 1); };
    t = IM_REGISTER_TEST(e, "hw", "loopback_sg2_osc2");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwLoopback(ctx, 2); };
    t = IM_REGISTER_TEST(e, "hw", "pause_inspect_buffer");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwPauseInspect(ctx); };
    t = IM_REGISTER_TEST(e, "hw", "reconnect_resends_sg2");
    t->TestFunc = [](ImGuiTestContext* ctx) { HwReconnectResendsSg2(ctx); };
}

// ---- Engine lifecycle ---------------------------------------------------------

void QaSetup(const char* run_filter)
{
    g_headless = (run_filter != nullptr);
    // Bare --qa means the stable regression suite; the fuzz categories are
    // opt-in (--qa=fuzz or individual test names).
    if (run_filter != nullptr && strcmp(run_filter, "all") == 0)
        run_filter = "gui,hw";

    g_engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(g_engine);
    io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    io.ConfigNoThrottle = g_headless;
    io.ConfigLogToTTY = g_headless;

    RegisterGuiTests(g_engine);
    RegisterFuzzTests(g_engine);
    RegisterPredictScenarios(g_engine);
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

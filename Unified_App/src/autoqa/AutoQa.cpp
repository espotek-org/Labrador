#include "autoqa/AutoQa.h"

#include "app/App.h"
#include "autoqa/HwTests.h"
#include "autoqa/Png.h"
#include "ui/Frontend.h"

#include "JsonWriter.h"
#include "librador.h"
#include "librador_platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace autoqa
{
// --- the screenshot matrix -------------------------------------------------
static const char* const THEMES[][2] = {
    {"classic-dark", "Classic Dark"},       {"classic-light", "Classic Light"},
    {"phosphor-retro", "Phosphor Retro"},   {"phosphor-modern", "Phosphor Modern"},
    {"amber-retro", "Amber Retro"},         {"amber-modern", "Amber Modern"},
    {"vector-retro", "Vector Retro"},       {"vector-modern", "Vector Modern"},
};
// desk_panel_page values (DesktopFrontend::Panel)
static const char* const PAGES[] = {"Scope", "Signals", "PSU", "Meter", "Logic", "DAQ", "Analysis"};

// Live signals for the board-connected shots, inside the app's limits at its
// default 4.5 V PSU (env hook read by every frontend, app/StartupSignal.h).
// Neither period is a multiple of the 1 ms USB frame (891 Hz: 1.122 ms, 557 Hz:
// 1.795 ms), so a dropped frame shows up in the screenshot as a kink in the sine
// or a mis-sized square pulse. A 1 kHz or 500 Hz signal repeats exactly every
// frame or two, so it looks perfect even when every other frame is lost.
static const char* const SG1_SIGNAL = "sine:891:2:0.5";
static const char* const SG2_SIGNAL = "square:557:1.5:0.3";

static const char* PAGE_EXPECT[] = {
    "the Scope page: acquisition, display, trigger, probe and math channel controls",
    "the Signals page with both signal generator sections switched on, their waveform pickers, "
    "frequency, Vpeak-peak and Vbase fields and small waveform previews",
    "the PSU page with the power supply voltage slider at 4.5 V",
    "the Meter page, which asks for multimeter mode while the inputs are in oscilloscope mode",
    "the Logic page with the logic analyser, decoder and digital output controls",
    "the DAQ page with the data logger controls",
    "the Analysis page with the spectrum and network analyser controls",
};

struct Shot
{
    App::LayoutMode layout;
    const char* layout_name;
    int width, height;
    int theme; // index into THEMES
    int page;
    float font_scale;
    bool board;

    std::string id() const
    {
        char text[160];
        const bool panel = layout == App::LayoutMode::Desktop || layout == App::LayoutMode::Tablet;
        std::string page_part = panel ? std::string(".") + PAGES[page] : std::string();
        std::transform(page_part.begin(), page_part.end(), page_part.begin(), ::tolower);
        std::snprintf(text, sizeof text, "gui.%s_%dx%d.%s%s.text%d.%s", layout_name, width, height,
                      THEMES[theme][0], page_part.c_str(), static_cast<int>(std::lround(font_scale * 100)),
                      board ? "board" : "noboard");
        return text;
    }

    std::string caption() const
    {
        char text[240];
        const bool panel = layout == App::LayoutMode::Desktop || layout == App::LayoutMode::Tablet;
        std::snprintf(text, sizeof text, "%s layout at %dx%d, %s theme%s%s, text size %g, %s", layout_name,
                      width, height, THEMES[theme][1], panel ? ", " : "", panel ? (std::string(PAGES[page]) + " page").c_str() : "",
                      font_scale, board ? "board connected, generators looped back" : "USB disabled (no board)");
        return text;
    }

    std::string expect() const
    {
        const std::string theme = THEMES[theme_index()][1];
        const std::string id = THEMES[theme_index()][0];
        const std::string style = id.rfind("classic", 0) == 0 ? "plain bordered panels"
                                                              : "a CRT-style bezel and segment frames";
        std::string plot;
        if (board)
            plot = "The plot shows two live traces, CH1 an 891 Hz sine between about 0.5 and 2.5 V (about "
                   "27 cycles across the 30 ms window) and CH2 a 557 Hz square between about 0.3 and 1.8 V "
                   "(about 17 cycles, high and low halves equal), and the board is reported connected. These "
                   "frequencies don't line up with the 1 ms USB frame, so a lost frame shows as a kink or step "
                   "in the sine or square pulses of uneven width; any of those, or a signal at twice its "
                   "frequency, means the USB link is losing frames, which is a failure.";
        else
            plot = "The plot is empty because USB is disabled, and the status reports no board.";
        const std::string text = font_scale > 1.0f ? " Text is drawn larger than normal and everything still fits."
                                 : font_scale < 1.0f ? " Text is drawn smaller than normal and stays readable."
                                                     : "";
        const std::string common = " No label is cut off, no widget overlaps another or spills out of its "
                                   "frame, and text contrasts clearly with its background.";
        switch (layout)
        {
        case App::LayoutMode::Desktop:
            return "The desktop layout in the " + theme + " theme (" + style +
                   "): menu bar and toolbar along the top, the plot filling the left, and the side panel "
                   "on the right showing " + PAGE_EXPECT[page] + ". " + plot + text + common;
        case App::LayoutMode::Tablet:
            return "The tablet layout (the desktop arrangement with touch-sized controls) in the " + theme +
                   " theme, side panel showing " + PAGE_EXPECT[page] + ". " + plot + text + common;
        case App::LayoutMode::Compact:
            return "The compact touch layout for 800x480 screens in the " + theme +
                   " theme: a large plot with big touch buttons around it, the SG1 and SG2 buttons naming "
                   "the running signals (an 891 Hz sine and a 557 Hz square). " + plot + text + common;
        default:
            if (board)
                plot = "The plot shows CH1's live 891 Hz sine between about 0.5 and 2.5 V as a smooth fast "
                       "oscillation across the whole window (this layout starts with only CH1 enabled).";
            return "The mobile tile layout in the " + theme + " theme: the plot above a row of control tiles "
                   "(Inputs, Logic Dec., DAQ) whose controls all fit inside the window. " + plot + text + common;
        }
    }

    int theme_index() const { return theme; }
};

static std::vector<Shot> shotMatrix()
{
    using L = App::LayoutMode;
    std::vector<Shot> shots;
    for (int t = 0; t < 8; t++) // every theme on the Scope page
        shots.push_back({L::Desktop, "desktop", 1280, 720, t, 0, 1.0f, true});
    for (const int t : {0, 2}) // every page in a classic and a CRT theme
        for (int p = 1; p < 7; p++)
            shots.push_back({L::Desktop, "desktop", 1280, 720, t, p, 1.0f, true});
    for (const float s : {0.85f, 1.2f, 1.45f}) // text sizes on the Scope and Signals pages
        for (const int p : {0, 1})
            shots.push_back({L::Desktop, "desktop", 1280, 720, 0, p, s, true});
    shots.push_back({L::Compact, "compact", 800, 480, 0, 0, 1.0f, true});
    shots.push_back({L::Compact, "compact", 800, 480, 2, 0, 1.0f, true});
    shots.push_back({L::Tablet, "tablet", 1024, 768, 0, 0, 1.0f, true});
    shots.push_back({L::Tablet, "tablet", 1280, 720, 5, 1, 1.0f, true});
    shots.push_back({L::Mobile, "mobile", 480, 720, 0, 0, 1.0f, true});
    // and the same app with USB switched off
    shots.push_back({L::Desktop, "desktop", 1280, 720, 0, 0, 1.0f, false});
    shots.push_back({L::Desktop, "desktop", 1280, 720, 2, 1, 1.0f, false});
    shots.push_back({L::Compact, "compact", 800, 480, 0, 0, 1.0f, false});
    shots.push_back({L::Tablet, "tablet", 1024, 768, 0, 0, 1.0f, false});
    shots.push_back({L::Mobile, "mobile", 480, 720, 0, 0, 1.0f, false});
    return shots;
}

// Seconds a shot waits before its frame is taken: the first board shot gives the
// app time to find, connect to and stream from the board; later ones only let
// the new layout, theme or size settle and the traces refill.
static constexpr double FIRST_BOARD_SETTLE_S = 6.0;
static constexpr double SHOT_SETTLE_S = 1.5;
static constexpr int SHOT_MIN_FRAMES = 10;
static constexpr double WINDOW_RESIZE_TIMEOUT_S = 5.0;

// --- environment facts for the report ---------------------------------------
static std::string isoNow()
{
    const std::time_t now = std::time(nullptr);
    const std::tm tm = *std::gmtime(&now); // main thread only
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return text;
}

static std::string osVersion()
{
#if defined(__APPLE__)
    char version[64];
    size_t size = sizeof version;
    if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0)
        throw std::runtime_error("sysctl kern.osproductversion failed");
    return version;
#elif defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtl = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtl)
        throw std::runtime_error("RtlGetVersion is missing");
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof info;
    rtl(&info);
    char text[64];
    std::snprintf(text, sizeof text, "%lu.%lu.%lu", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
    return text;
#else
    std::ifstream release("/etc/os-release");
    for (std::string line; std::getline(release, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0)
        {
            std::string v = line.substr(12);
            if (v.size() >= 2 && v.front() == '"')
                v = v.substr(1, v.size() - 2);
            return v;
        }
    throw std::runtime_error("/etc/os-release has no PRETTY_NAME");
#endif
}

static std::string hostName()
{
#if defined(_WIN32)
    char name[256];
    DWORD size = sizeof name;
    if (!GetComputerNameA(name, &size))
        throw std::runtime_error("GetComputerName failed");
    return name;
#else
    char name[256];
    if (gethostname(name, sizeof name) != 0)
        throw std::runtime_error("gethostname failed");
    name[sizeof name - 1] = '\0';
    return name;
#endif
}

static const char* archName()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
}

// --- AutoQa --------------------------------------------------------------------
static librador_host_hooks g_saved_hooks;
static librador_logger_p g_saved_logger = nullptr;
static void* g_saved_logger_userdata = nullptr;

AutoQa::AutoQa(std::string out_dir, std::string exe_path)
    : m_out(std::move(out_dir)), m_exe(std::move(exe_path)), m_started_at(isoNow()), m_t0(nowSeconds()),
      m_runner(m_out, [this](const std::string& line) { log(line); })
{
    if (!SDL_CreateDirectory(m_out.c_str()))
        throw std::runtime_error("cannot create " + m_out + ": " + SDL_GetError());
    const std::string progress = m_out + "/progress.log";
    m_progress = std::fopen(progress.c_str(), "w");
    if (!m_progress)
        throw std::runtime_error("cannot create " + progress);
    log(std::string("labrador --auto-qa ") + m_out + " (" + m_exe + ")");

    // The run must not depend on the caller's environment: the testing hooks the
    // app reads are cleared, and both generators start on so the board-connected
    // screenshots show live traces.
    SDL_Environment* env = SDL_GetEnvironment();
    for (const char* var : {"LABRADOR_NO_USB", "LABRADOR_LAYOUT", "LABRADOR_WINDOW_SIZE", "LABRADOR_FRAME_DUMP"})
        SDL_UnsetEnvironmentVariable(env, var);
    SDL_SetEnvironmentVariable(env, "LABRADOR_SG1", SG1_SIGNAL, true);
    SDL_SetEnvironmentVariable(env, "LABRADOR_SG2", SG2_SIGNAL, true);

    m_shots = shotMatrix();
    m_runner.setPlanned(hardwareTestCount() + static_cast<int>(m_shots.size()));
    m_runner.setOnRecorded([this]() { writeReport(false); });
}

AutoQa::~AutoQa()
{
    if (m_worker.joinable())
        m_worker.join();
    if (m_progress)
        std::fclose(m_progress);
}

std::string AutoQa::settingsPath() const
{
    return m_out + "/isolated-settings/settings.ini";
}

void AutoQa::log(const std::string& line)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "[%7.1f s] ", nowSeconds() - m_t0);
    std::fprintf(m_progress, "%s%s\n", stamp, line.c_str());
    std::fflush(m_progress);
    std::printf("auto-qa %s%s\n", stamp, line.c_str());
    std::fflush(stdout);
}

std::string AutoQa::statusText() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_status;
}

void AutoQa::frame(App& app)
{
    switch (m_phase)
    {
    case Phase::Start: startHardware(app); break;
    case Phase::Hardware:
        if (m_worker_done)
            finishHardware(app);
        break;
    case Phase::Shots: shotFrame(app); break;
    case Phase::Firmware:
        if (m_worker_done)
            finishFirmware(app);
        break;
    case Phase::Done: break;
    }
}

void AutoQa::startWorker(std::function<void()> work)
{
    // The worker opens its own librador sessions; the app's own one (and its
    // log sink and flash-request hook, which would queue a flash dialog when the
    // round trip leaves old firmware on the board) is set aside until it is done.
    g_saved_hooks = librador_get_host_hooks();
    g_saved_logger = librador_logger_get();
    g_saved_logger_userdata = librador_logger_get_userdata();
    librador_set_host_hooks(librador_host_hooks());
    librador_exit();
    m_worker_done = false;
    m_worker_error.clear();
    m_worker = std::thread([this, work]() {
        try
        {
            work();
        }
        catch (const std::exception& e)
        {
            m_worker_error = e.what(); // reported as its own failed test by the caller
        }
        m_worker_done = true;
    });
}

void AutoQa::restoreAppSession()
{
    m_worker.join();
    if (librador_init(LABRADOR_TRANSPORT_AUTO) < 0)
        throw std::runtime_error("librador_init failed after the auto-QA worker");
    librador_logger_set(g_saved_logger_userdata, g_saved_logger);
    librador_set_host_hooks(g_saved_hooks);
}

void AutoQa::startHardware(App& app)
{
    (void)app;
    log("phase 1/3: hardware tests");
    m_phase = Phase::Hardware;
    startWorker([this]() {
        runHardwareTests(m_runner, m_out, [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status = s;
        });
    });
}

void AutoQa::finishHardware(App& app)
{
    restoreAppSession();
    if (!m_worker_error.empty())
        m_runner.run("hw.suite", "hw", "The hardware test sequence ran to the end", {},
                     [this](TestContext& ctx) { ctx.fail("the hardware tests stopped early: " + m_worker_error); });
    (void)app;
    log("phase 2/3: screenshot tour (" + std::to_string(m_shots.size()) + " shots)");
    m_phase = Phase::Shots;
    m_step = ShotStep::Setup;
}

void AutoQa::startFirmware(App& app)
{
    (void)app;
    log("phase 3/3: firmware round trip");
    m_phase = Phase::Firmware;
    startWorker([this]() {
        runFirmwareRoundTrip(m_runner, m_out, [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status = s;
        });
    });
}

void AutoQa::finishFirmware(App& app)
{
    restoreAppSession();
    if (!m_worker_error.empty())
        m_runner.run("firmware.suite", "firmware", "The firmware round trip ran to the end", {},
                     [this](TestContext& ctx) { ctx.fail("the firmware round trip stopped early: " + m_worker_error); });
    finish(app);
}

void AutoQa::shotFrame(App& app)
{
    if (m_shot >= m_shots.size())
    {
        startFirmware(app);
        return;
    }
    const Shot& shot = m_shots[m_shot];
    switch (m_step)
    {
    case ShotStep::Setup:
    {
        if (!shot.board && !m_usb_disabled)
        {
            app.disableUsb();
            m_usb_disabled = true;
        }
        if (!m_runner.begin(shot.id(), "gui", "Screenshot: " + shot.caption(),
                            shot.board ? std::vector<std::string>{"board"} : std::vector<std::string>{}))
        {
            m_shot++; // precondition unmet: recorded already
            return;
        }
        app.setLayoutMode(shot.layout);
        app.setThemeId(THEMES[shot.theme][0]);
        app.setFontScale(shot.font_scale);
        app.setWindowSize(shot.width, shot.height);
        app.showSidePanelPage(shot.page);
        m_step = ShotStep::Settle;
        m_step_start = nowSeconds();
        m_step_frames = 0;
        m_stable_frames = 0;
        return;
    }
    case ShotStep::Settle:
    {
        // The side panel page needs the frontend for this layout, which exists
        // from the first frame after the layout switch.
        app.showSidePanelPage(shot.page);
        m_step_frames++;
        int w, h;
        app.windowPixelSize(&w, &h);
        m_stable_frames = (w == m_last_w && h == m_last_h) ? m_stable_frames + 1 : 0;
        m_last_w = w;
        m_last_h = h;
        const double settle = (shot.board && !m_board_seen_streaming) ? FIRST_BOARD_SETTLE_S : SHOT_SETTLE_S;
        const double elapsed = nowSeconds() - m_step_start;
        if (m_step_frames >= SHOT_MIN_FRAMES && elapsed >= settle &&
            (m_stable_frames >= 3 || elapsed >= settle + WINDOW_RESIZE_TIMEOUT_S))
        {
            app.requestFrameCapture();
            m_step = ShotStep::Capture;
        }
        return;
    }
    case ShotStep::Capture:
    {
        int w = 0, h = 0;
        std::vector<unsigned char> rgb;
        if (!app.takeFrameCapture(&w, &h, &rgb))
            return; // the capture happens after this frame renders
        std::string failure, skip, error;
        try
        {
            // The Runner's open context for this shot.
            TestContext* ctx = m_runner.openContext();
            evaluateShot(app, *ctx, shot, w, h, rgb);
        }
        catch (const TestFailed& e)
        {
            failure = e.message;
        }
        catch (const TestSkipped& e)
        {
            skip = e.message;
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        m_runner.finish(failure, skip, error);
        m_shot++;
        m_step = ShotStep::Setup;
        return;
    }
    }
}

void AutoQa::evaluateShot(App& app, TestContext& ctx, const Shot& shot, int w, int h,
                          const std::vector<unsigned char>& rgb)
{
    const std::string png = "screenshots/" + shot.id() + ".png";
    if (!SDL_CreateDirectory((m_out + "/screenshots").c_str()))
        throw std::runtime_error(std::string("cannot create screenshots/: ") + SDL_GetError());
    writePngRgb(m_out + "/" + png, w, h, rgb);
    ctx.artifact(png, "png", shot.caption());
    m_screenshots.push_back({shot.id(), png, shot.caption(), shot.expect()});

    const std::string status = app.statusLine();
    ctx.note(status);
    std::map<std::string, std::string> fields;
    std::istringstream in(status);
    for (std::string item; in >> item;)
    {
        const size_t eq = item.find('=');
        if (eq != std::string::npos)
            fields[item.substr(0, eq)] = item.substr(eq + 1);
    }
    ctx.measureText("layout", fields.at("layout"), shot.layout_name);
    ctx.measureText("theme", fields.at("theme"), THEMES[shot.theme][0]);
    ctx.measure("text size", "", std::stod(fields.at("font_scale")), shot.font_scale, shot.font_scale - 0.005,
                shot.font_scale + 0.005);
    const std::string board = fields.at("board");
    ctx.measureText("board", board, shot.board ? "streaming" : "usb-disabled");
    if (shot.board && board == "streaming")
        m_board_seen_streaming = true;
    for (const char* flag : {"safety_mode", "uninitialised_mode", "flashing"})
        ctx.measure(std::string(flag) + " warning", "", std::stod(fields.at(flag)), 0, 0, 0);
    if (fields.count("frames_dropped"))
        ctx.measure("USB frames dropped since connecting", "", std::stod(fields.at("frames_dropped")));

    int ww, wh;
    app.windowSize(&ww, &wh);
    const double scale = static_cast<double>(w) / shot.width;
    ctx.measure("window width", "pt", ww, shot.width, shot.width, shot.width);
    ctx.measure("window height", "pt", wh, shot.height, shot.height, shot.height);
    ctx.measure("pixel scale (frame width / window width)", "x", scale, std::nullopt, 1.0, 3.0);
    ctx.measure("frame height", "px", h, std::round(shot.height * scale), std::round(shot.height * scale) - 1,
                std::round(shot.height * scale) + 1);

    // Not blank: plenty of colours, no single colour filling the frame, and
    // some luminance spread (sampled on every fourth pixel both ways).
    std::map<uint32_t, int> counts;
    double sum = 0, sum2 = 0;
    int n = 0;
    for (int y = 0; y < h; y += 4)
        for (int x = 0; x < w; x += 4)
        {
            const unsigned char* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
            counts[(p[0] << 16) | (p[1] << 8) | p[2]]++;
            const double l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
            sum += l;
            sum2 += l * l;
            n++;
        }
    int top = 0;
    for (const auto& kv : counts)
        top = std::max(top, kv.second);
    const double mean = sum / n;
    ctx.measure("distinct colours (every 4th pixel)", "", static_cast<double>(counts.size()), std::nullopt, 64);
    ctx.measure("most common colour's share of the frame", "fraction", static_cast<double>(top) / n,
                std::nullopt, std::nullopt, 0.95);
    ctx.measure("luminance standard deviation", "", std::sqrt(std::max(0.0, sum2 / n - mean * mean)),
                std::nullopt, 8.0);
}

void AutoQa::finish(App& app)
{
    writeReport(true);
    // The isolated settings are discarded: nothing of this run stays behind.
    const std::string settings = settingsPath();
    if (SDL_GetPathInfo(settings.c_str(), nullptr) && !SDL_RemovePath(settings.c_str()))
        throw std::runtime_error("cannot remove " + settings + ": " + SDL_GetError());
    const std::string dir = m_out + "/isolated-settings";
    if (SDL_GetPathInfo(dir.c_str(), nullptr) && !SDL_RemovePath(dir.c_str()))
        throw std::runtime_error("cannot remove " + dir + ": " + SDL_GetError());

    int pass = 0, fail = 0, skip = 0, error = 0;
    for (const TestResult& t : m_runner.results())
        (t.status == "pass" ? pass : t.status == "fail" ? fail : t.status == "skip" ? skip : error)++;
    char line[200];
    std::snprintf(line, sizeof line, "done: %d passed, %d failed, %d errors, %d skipped in %.0f s; report %s/report.json",
                  pass, fail, error, skip, nowSeconds() - m_t0, m_out.c_str());
    log(line);
    for (const TestResult& t : m_runner.results())
        if (t.status == "fail" || t.status == "error")
            log("  " + t.status + " " + t.id + ": " + t.message.substr(0, 300));
    m_phase = Phase::Done;
    app.finishAutoQa(fail == 0 && error == 0 ? 0 : 1);
}

void AutoQa::writeReport(bool complete)
{
    int pass = 0, fail = 0, skip = 0, error = 0;
    for (const TestResult& t : m_runner.results())
        (t.status == "pass" ? pass : t.status == "fail" ? fail : t.status == "skip" ? skip : error)++;
    const std::string path = m_out + "/report.json";
    const std::string tmp = path + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        JsonWriter json(file);
        json.beginObject();
        json.field("schema_version", 2); // 2: rewritten after every test, with "complete"
        json.key("suite").beginObject();
        json.field("name", "labrador --auto-qa");
        json.key("commit");
        if (std::string(LABRADOR_GIT_SHA).empty())
            json.null();
        else
            json.value(LABRADOR_GIT_SHA);
        json.endObject();
        json.key("app").beginObject();
        json.field("version", LABRADOR_VERSION);
        json.field("labrador", m_exe);
        json.key("labrador_cli").null();
        char firmware[40];
        std::snprintf(firmware, sizeof firmware, "labrafirm_%04X_%02u.hex",
                      static_cast<unsigned>(EXPECTED_FIRMWARE_VERSION),
                      static_cast<unsigned>(DEFINED_EXPECTED_VARIANT));
        json.field("firmware", firmware);
        json.endObject();
        json.key("platform").beginObject();
        json.field("os", SDL_GetPlatform());
        json.field("os_version", osVersion());
        json.field("arch", archName());
        json.field("hostname", hostName());
        json.endObject();
        json.field("started_at", m_started_at);
        json.field("finished_at", isoNow());
        json.field("complete", complete);
        json.field("result", fail == 0 && error == 0 ? "pass" : "fail");
        json.key("counts").beginObject();
        json.field("pass", pass);
        json.field("fail", fail);
        json.field("skip", skip);
        json.field("error", error);
        json.endObject();
        json.key("tests").beginArray();
        for (const TestResult& t : m_runner.results())
            writeTestJson(json, t);
        json.endArray();
        json.key("screenshots").beginArray();
        for (const Screenshot& s : m_screenshots)
        {
            json.beginObject();
            json.field("id", s.id);
            json.field("path", s.path);
            json.field("caption", s.caption);
            json.field("expect", s.expect);
            json.endObject();
        }
        json.endArray();
        json.endObject();
        json.finish();
        if (!file)
            throw std::runtime_error("writing " + tmp + " failed");
    }
    if (!SDL_RenamePath(tmp.c_str(), path.c_str()))
        throw std::runtime_error("cannot rename " + tmp + ": " + SDL_GetError());
}
} // namespace autoqa

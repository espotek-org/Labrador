#pragma once

#include "AppBase.h"
#include "app/settings.h"
#include "app/DebugConsole.h"
#include "ui/Frontend.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

// Shared session controller. Owns everything form-factor-independent: SDL/GL
// lifecycle (via AppBase), the librador connection + polling, firmware /
// bootloader / gobindar recovery flows and their popups, safety/uninitialised
// detection, settings persistence and the debug console. It selects one
// Frontend by platform + resolution and drives it each frame; the frontend
// owns its own widgets and layout (see src/ui/). See docs/PLAN.md.
class App : public AppBase
{
  public:
    App() : AppBase("EspoTek Labrador") {}

  protected:
    void StartUp() override;
    void Update() override;
    void ShutDown() override;

  public:
    // Layouts: desktop = the Monash arrangement (DesktopFrontend), mobile =
    // Brent's tile design (AndroidFrontend), compact = 800x480 Raspberry Pi
    // LCDs (LowResFrontend), tablet = the desktop layout under a touch-sized
    // style for 1024x768 / 1280x720-class Android tablets (TabletFrontend).
    enum class LayoutMode { Auto, Desktop, Mobile, Compact, Tablet };

    // ---- Interface used by the Frontends -----------------------------------
    // Connection / hardware status.
    bool isConnected() const { return connected; }
    void setConnected(bool c) { connected = c; }
    bool isFlashing() const { return m_flashing; }
    bool isRecovering() const { return m_recovering; }
    bool safetyMode() const { return safety_mode; }
    bool uninitialisedMode() const { return uninitialised_mode; }
    bool bootloaderSeen() const { return m_bootloader_seen; }
    bool devicePresent() const { return m_device_present; }

    // Layout selection (View menu).
    LayoutMode layoutMode() const { return m_layout_mode; }
    void setLayoutMode(LayoutMode m) { m_layout_mode = m; }

    // Theme (View menu). The app re-applies SetGlobalStyle every frame, so
    // the setter just records the id (must be one of the ThemeSpec table's).
    const std::string& themeId() const { return m_theme; }
    void setThemeId(const char* id) { m_theme = id; }

    // Shared debug/help toggles owned by App but toggled from the desktop menu
    // bar (returned by reference so ImGui::MenuItem can bind to them).
    bool& showDebugConsole() { return m_show_debug_console; }
    bool& showShortcuts() { return m_show_shortcuts; }

    // Device > Reflash firmware — reuses the firmware-mismatch modal flow.
    void requestFirmwareReflash() { m_firmware_mismatch = true; }

    // File > Quit (RequestQuit is protected on AppBase).
    void requestQuit() { RequestQuit(); }

    // Frame counter + control-resend cadence (for widget servicing throttling).
    int frames() const { return m_frames; }
    int labRefreshRate() const { return m_lab_refresh_rate; }

  private:
    LayoutMode resolvedLayout() const;
    void ensureFrontend();

    void pollDevice();
    void drawFirmwarePopup();
    void drawGobindarPopup();
    void startFirmwareFlash();
    void startBootloaderRecovery();
    void startGobindarRecovery();
    void loadSettings();
    void pushSettings();

    LayoutMode m_layout_mode = LayoutMode::Auto;

    // One Frontend per form factor, created lazily the first time its layout
    // is resolved (display size is only known once frames start). m_active_*
    // tracks which one is currently being driven.
    std::unique_ptr<Frontend> m_desktop_frontend;
    std::unique_ptr<Frontend> m_lowres_frontend;
    std::unique_ptr<Frontend> m_android_frontend;
    std::unique_ptr<Frontend> m_tablet_frontend;
    Frontend* m_active_frontend = nullptr;

    // Persistent settings (settings.ini in getPrefPath()). Live UI state is
    // pushed into the store every frame and flushed on change (throttled),
    // because Android may never call ShutDown cleanly.
    Settings m_settings;
    std::string m_theme = "phosphor";
    bool m_layout_env_override = false; // LABRADOR_LAYOUT wins but is not saved

    int m_frames = 0;
    const int m_lab_refresh_rate = 60; // send controls to labrador every this many frames
    bool connected = false;
    bool m_show_debug_console = false;
    bool m_show_shortcuts = false;
    DebugConsole m_debug_console;
    bool safety_mode = false;
    bool uninitialised_mode = false;
    double m_connected_since = -1.0; // ImGui time of the connected transition
    // Time-based debounce (was frame-based, which tripped on millisecond
    // transients at high frame rates — 240 Hz monitors, the --qa runner).
    double uninit_enter_s = 0.0;
    double uninit_exit_s = 0.0;
    const double UNINIT_ENTER_THRESHOLD_S = 0.17; // sustained time to enter
    const double UNINIT_EXIT_THRESHOLD_S = 1.0;   // sustained time to exit
    // Wedge auto-heal: spend up to two automatic USB resets before showing
    // the disconnect-and-reconnect popup. The budget is only forgiven after
    // a sustained healthy period — NOT on disconnect, because our own reset
    // causes one (that would re-arm an infinite reset loop).
    int m_uninit_auto_resets = 0;
    double m_uninit_healthy_s = 0.0;
    // Stream-property wedge check: a wedged device stops (or never starts)
    // delivering frames, which no sample-based check can see — an idle
    // buffer just replays its stale contents.
    uint64_t m_stream_total_last = 0;
    double m_stream_check_t = -1.0;
    double m_stream_stall_s = 0.0;
    // Wall-clock timestamp of the previous Update, for the wedge debounce
    // accumulators (capped per frame so a debugger pause can't jump them).
    double m_uninit_last_wall_s = -1.0;

    // Desktop device polling (Android connects via USB attach intents)
    double m_last_poll_time = 0.0;
    bool m_device_present = false;
    bool m_bootloader_seen = false;
    // Auto-recovery of boards stuck in bootloader mode: require a steady
    // sighting before acting, and act once per bootloader episode.
    int m_bootloader_streak = 0;
    bool m_recovery_attempted = false;
    std::atomic<bool> m_recovering { false };

    // Gobindar (PID 0xa000, misconfigured firmware) recovery: user shorts
    // DO1 to GND and replugs; the worker waits for bootloader mode and flashes.
    int m_gobindar_streak = 0;
    bool m_gobindar_attempted = false;
    bool m_gobindar_open = false; // dialog visible (main thread only)
    std::atomic<bool> m_gobindar_done { false };
    std::atomic<int> m_gobindar_result { 0 };
    intptr_t m_gobindar_texture = 0;
    int m_gobindar_img_w = 0;
    int m_gobindar_img_h = 0;

    // Firmware flash flow: librador's request_firmware_flash hook raises the
    // flag; the modal runs the blocking flash on a worker thread.
    std::atomic<bool> m_firmware_mismatch { false };
    std::atomic<bool> m_flashing { false };
    std::atomic<int> m_flash_result { 0 };
    std::atomic<bool> m_flash_finished { false };
    std::thread m_flash_thread;

    friend struct AppHooks;
};

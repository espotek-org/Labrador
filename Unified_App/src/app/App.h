#pragma once

#include "AppBase.h"
#include "app/settings.h"
#include "app/DebugConsole.h"
#include "ui/Frontend.h"

#include <atomic>
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
    // LCDs (LowResFrontend).
    enum class LayoutMode { Auto, Desktop, Mobile, Compact };

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

    // Theme (View menu). The app re-applies SetGlobalStyle every frame, so the
    // setter just flips the flag.
    bool darkTheme() const { return m_dark_theme; }
    void setDarkTheme(bool dark) { m_dark_theme = dark; }

    // Shared debug/help toggles owned by App but toggled from the desktop menu
    // bar (returned by reference so ImGui::MenuItem can bind to them).
    bool& showDemoWindows() { return m_show_demo_windows; }
    bool& showDebugConsole() { return m_show_debug_console; }
    bool& showShortcuts() { return m_show_shortcuts; }

    // Device > Reflash firmware — reuses the firmware-mismatch modal flow.
    void requestFirmwareReflash() { m_firmware_mismatch = true; }

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
    Frontend* m_active_frontend = nullptr;

    // Persistent settings (settings.ini in getPrefPath()). Live UI state is
    // pushed into the store every frame and flushed on change (throttled),
    // because Android may never call ShutDown cleanly.
    Settings m_settings;
    bool m_dark_theme = true;
    bool m_layout_env_override = false; // LABRADOR_LAYOUT wins but is not saved

    int m_frames = 0;
    const int m_lab_refresh_rate = 60; // send controls to labrador every this many frames
    bool connected = false;
    bool m_show_demo_windows = false;
    bool m_show_debug_console = false;
    bool m_show_shortcuts = false;
    DebugConsole m_debug_console;
    bool safety_mode = false;
    bool uninitialised_mode = false;
    int uninit_enter_count = 0;
    int uninit_exit_count = 0;
    const int UNINIT_ENTER_THRESHOLD = 10; // frames to enter
    const int UNINIT_EXIT_THRESHOLD = 60;  // frames to exit

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

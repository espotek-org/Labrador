#include "App.h"

#include "implot.h"
#include "librador.h"
#include "instruments/util.h"
#include "app/textures.h"
#include "platform/paths.h"
#include "ui/desktop/DesktopFrontend.h"
#include "ui/lowres/LowResFrontend.h"
#include "ui/android/AndroidFrontend.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

// librador host hooks are plain function pointers (no user_data), so route
// them through the single App instance.
static App* g_app = nullptr;

struct AppHooks
{
    static void requestFirmwareFlash() { g_app->m_firmware_mismatch = true; }
};

void App::StartUp()
{
    g_app = this;

    // Console must be installed before librador starts logging from threads
    m_debug_console.install();

    if (librador_init() < 0)
        throw std::runtime_error("librador_init failed");

#ifndef __ANDROID__
    // On Android the JNI glue registered its hooks in main(); the firmware
    // dialogs there are native Java, so don't replace them with ours.
    librador_host_hooks hooks;
    hooks.request_firmware_flash = &AppHooks::requestFirmwareFlash;
    librador_set_host_hooks(hooks);
#endif

    // Gobindar-recovery wiring diagram (the recovery flow lives in App). The
    // instrument pinout textures load in InstrumentFrontend::startUp instead.
    std::vector<unsigned char> gob_png = loadAsset("media/gobindar-diagram.png");
    unsigned int gob_tmp_texture = 0;
    if (LoadTextureFromMemory(gob_png.data(), gob_png.size(), &gob_tmp_texture,
            &m_gobindar_img_w, &m_gobindar_img_h))
        m_gobindar_texture = (intptr_t)gob_tmp_texture;

    loadSettings();
    SetGlobalStyle(m_dark_theme);

    // Layout override for testing (e.g. LABRADOR_LAYOUT=compact ./labrador --smoke).
    // It wins for this run but is never written back to settings.ini.
    if (const char* layout_env = SDL_getenv("LABRADOR_LAYOUT"))
    {
        std::string l(layout_env);
        if (l == "desktop") m_layout_mode = LayoutMode::Desktop;
        else if (l == "mobile") m_layout_mode = LayoutMode::Mobile;
        else if (l == "compact") m_layout_mode = LayoutMode::Compact;
        else
            l.clear();
        m_layout_env_override = !l.empty();
    }
}

void App::loadSettings()
{
    m_settings.load();

    const std::string layout = m_settings.getString("layout", "auto");
    if (layout == "desktop") m_layout_mode = LayoutMode::Desktop;
    else if (layout == "mobile") m_layout_mode = LayoutMode::Mobile;
    else if (layout == "compact") m_layout_mode = LayoutMode::Compact;
    else m_layout_mode = LayoutMode::Auto;

    m_dark_theme = m_settings.getString("theme", "dark") != "light";

    // Widget-specific keys (hw_gain, hw_gain_auto, cal_*) are loaded by the
    // frontend once it is created (InstrumentFrontend::loadSettings), since the
    // widgets live there. PSU voltage is deliberately NOT persisted:
    // PSUControl::controlLab re-sends the set voltage periodically once a board
    // connects, so restoring a saved high voltage would energize whatever
    // circuit is wired to the PSU output on launch without any user action.
}

// Copy the live shared UI state into the settings store. Setters only mark the
// store dirty when a value actually changes, so this runs every frame; the
// throttled saveIfDirty in Update does the disk writes. Widget-specific keys
// are written by the active frontend's saveSettings.
void App::pushSettings()
{
    if (!m_layout_env_override)
    {
        const char* layout = "auto";
        switch (m_layout_mode)
        {
        case LayoutMode::Desktop: layout = "desktop"; break;
        case LayoutMode::Mobile: layout = "mobile"; break;
        case LayoutMode::Compact: layout = "compact"; break;
        default: break;
        }
        m_settings.set("layout", layout);
    }
    m_settings.set("theme", m_dark_theme ? "dark" : "light");
}

App::LayoutMode App::resolvedLayout() const
{
    if (m_layout_mode != LayoutMode::Auto)
        return m_layout_mode;
#ifdef __ANDROID__
    return LayoutMode::Mobile;
#else
    // 800x480-class LCDs (Raspberry Pi) get the compact layout
    if (ImGui::GetIO().DisplaySize.y <= 520.0f)
        return LayoutMode::Compact;
    return LayoutMode::Desktop;
#endif
}

// Select (creating lazily) the Frontend for the currently resolved layout and
// point m_active_frontend at it. Each form factor's frontend is created once
// and cached, so toggling the View menu between layouts preserves each one's
// state. Runs before pollDevice each frame so onDeviceConnected has a target.
void App::ensureFrontend()
{
    LayoutMode want = resolvedLayout();

    std::unique_ptr<Frontend>* slot = nullptr;
    switch (want)
    {
    case LayoutMode::Mobile: slot = &m_android_frontend; break;
    case LayoutMode::Compact: slot = &m_lowres_frontend; break;
    default:
        want = LayoutMode::Desktop;
        slot = &m_desktop_frontend;
        break;
    }

    if (!*slot)
    {
        switch (want)
        {
        case LayoutMode::Mobile: *slot = std::make_unique<AndroidFrontend>(); break;
        case LayoutMode::Compact: *slot = std::make_unique<LowResFrontend>(); break;
        default: *slot = std::make_unique<DesktopFrontend>(); break;
        }
        (*slot)->startUp(*this);
        (*slot)->loadSettings(m_settings);
    }

    m_active_frontend = slot->get();
}

void App::pollDevice()
{
#ifdef __ANDROID__
    // Connection is driven by MainActivity (USB attach intents + fd injection);
    // just track transitions so post-connect init runs, as in Brent's app.
    bool now_connected = librador_is_connected() && librador_iso_thread_is_active();
    if (now_connected && !connected && m_active_frontend)
        m_active_frontend->onDeviceConnected(*this);
    connected = now_connected;
    m_device_present = connected;
    return;
#else
    if (m_flashing)
        return; // the flash worker owns the USB state right now

    double now = ImGui::GetTime();
    if (now - m_last_poll_time < 0.5)
        return;
    m_last_poll_time = now;

    bool bootloader = false;
    bool gobindar = false;
    bool any_present = librador_device_present_ex(&bootloader, &gobindar);
    m_device_present = any_present && !gobindar; // legacy meaning: app or bootloader
    m_bootloader_seen = bootloader;

    // Gobindar-state board (PID 0xa000, misconfigured firmware): after a
    // steady sighting, show the recovery dialog and start the worker that
    // waits for the user's short-DO1-and-replug, then flashes. Once per
    // episode, like the bootloader recovery below.
    m_gobindar_streak = gobindar ? m_gobindar_streak + 1 : 0;
    if (m_gobindar_streak >= 3 && !m_gobindar_attempted)
    {
        m_gobindar_attempted = true;
        m_gobindar_open = true;
        startGobindarRecovery();
        return;
    }
    if (!any_present)
        m_gobindar_attempted = false;

    // Auto-recover boards stuck in bootloader mode (e.g. an interrupted
    // flash): after a steady sighting, try launch-then-reflash — once per
    // bootloader episode so a genuinely broken board can't cause a flash loop.
    // Caveat: this will also eject a board a user deliberately put in DFU mode
    // for external tools; close this app first in that workflow.
    m_bootloader_streak = (bootloader && !librador_is_connected()) ? m_bootloader_streak + 1 : 0;
    if (m_bootloader_streak >= 3 && !m_recovery_attempted)
    {
        m_recovery_attempted = true;
        startBootloaderRecovery();
        return;
    }
    if (!m_device_present)
        m_recovery_attempted = false; // board left: next appearance is a new episode

    if (m_device_present && !bootloader && !librador_is_connected())
    {
        if (librador_connect() >= 0 && librador_is_connected())
        {
            connected = true;
            m_recovery_attempted = false;
            if (m_active_frontend)
                m_active_frontend->onDeviceConnected(*this);
        }
    }
    else if (!m_device_present && librador_is_connected())
    {
        librador_disconnect();
        connected = false;
    }
    connected = librador_is_connected();
#endif
}

void App::startFirmwareFlash()
{
#ifdef __ANDROID__
    // Bootloader jump; MainActivity's USB attach events drive the actual flash.
    librador_initiate_firmware_flash();
#else
    char hex_name[64];
    snprintf(hex_name, sizeof hex_name, "firmware/labrafirm_%04hu_%02hhu.hex",
        constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
    std::string hex_path = getResourcePath(hex_name);
    m_flashing = true;
    m_flash_finished = false;
    if (m_flash_thread.joinable())
        m_flash_thread.join();
    m_flash_thread = std::thread([this, hex_path]() {
        m_flash_result = librador_flash_firmware(hex_path.c_str());
        m_flashing = false;
        m_flash_finished = true;
    });
#endif
}

void App::startBootloaderRecovery()
{
#ifndef __ANDROID__
    char hex_name[64];
    snprintf(hex_name, sizeof hex_name, "firmware/labrafirm_%04hu_%02hhu.hex",
        constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
    std::string hex_path = getResourcePath(hex_name);
    m_flashing = true;
    m_recovering = true;
    m_flash_finished = false;
    if (m_flash_thread.joinable())
        m_flash_thread.join();
    m_flash_thread = std::thread([this, hex_path]() {
        int result = librador_bootloader_recover(hex_path.c_str());
        // 1 = connected but firmware mismatch: the hook has queued the flash
        // popup, so that path is not a recovery failure.
        m_flash_result = (result == 1) ? 0 : result;
        m_flashing = false;
        m_recovering = false;
        // Only surface the result dialog on failure — a silently rescued
        // board just comes back as connected.
        if (m_flash_result != 0)
            m_flash_finished = true;
    });
#endif
}

void App::startGobindarRecovery()
{
#ifndef __ANDROID__
    char hex_name[64];
    snprintf(hex_name, sizeof hex_name, "firmware/labrafirm_%04hu_%02hhu.hex",
        constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
    std::string hex_path = getResourcePath(hex_name);
    m_flashing = true;
    m_gobindar_done = false;
    if (m_flash_thread.joinable())
        m_flash_thread.join();
    m_flash_thread = std::thread([this, hex_path]() {
        m_gobindar_result = librador_gobindar_recover(hex_path.c_str());
        m_flashing = false;
        m_gobindar_done = true;
    });
#endif
}

void App::drawGobindarPopup()
{
    if (m_gobindar_open && !ImGui::IsPopupOpen("Sorry to Interrupt!"))
        ImGui::OpenPopup("Sorry to Interrupt!");

    if (ImGui::BeginPopupModal("Sorry to Interrupt!", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Qt's wording (genericusbdriver.cpp GobindarDialog)
        ImGui::TextWrapped("Your board is misconfigured!\n"
                           "Please enable Bootloader mode to repair the issue.");
        ImGui::Spacing();
        ImGui::TextWrapped("To do this, connect Digital Out 1 to GND (as shown below), "
                           "then reconnect the board to your computer.");
        if (m_gobindar_texture != 0)
        {
            // Scale the wiring diagram to a sane width
            float img_w = 420.0f;
            float img_h = img_w * (float)m_gobindar_img_h / (float)m_gobindar_img_w;
            ImGui::Image((ImTextureID)m_gobindar_texture, ImVec2(img_w, img_h));
        }
        ImGui::Spacing();
        if (!m_gobindar_done)
        {
            ImGui::TextColored(constants::GRAY_TEXT,
                "Waiting for the board to appear in bootloader mode\xe2\x80\xa6");
        }
        else
        {
            const int result = m_gobindar_result.load();
            if (result == 0 || result == 1)
                ImGui::TextWrapped("Board repaired and reconnected successfully.");
            else if (result == -10)
                ImGui::TextWrapped("Timed out waiting for bootloader mode.\n"
                                   "Unplug the board and try again.");
            else
                ImGui::TextWrapped("Recovery failed (code %d).\n"
                                   "Unplug the board and try again.", result);
            if (ImGui::Button(" OK "))
            {
                m_gobindar_open = false;
                m_gobindar_done = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void App::drawFirmwarePopup()
{
    if (m_firmware_mismatch.exchange(false))
        ImGui::OpenPopup("Flash Firmware");

    if (ImGui::BeginPopupModal("Flash Firmware", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (m_flashing)
        {
            ImGui::TextColored(ImVec4(1, 0, 0, 1),
                "Flashing firmware %hu.%hhu\xe2\x80\xa6 this takes a few seconds.",
                constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Do not unplug the board.");
        }
        else if (m_flash_finished)
        {
            if (m_flash_result == 0)
                ImGui::TextWrapped("Firmware updated successfully.");
            else
                ImGui::TextWrapped("Firmware flash failed (code %d).\n"
                                   "Unplug and replug the board, then try again.",
                    m_flash_result.load());
            if (ImGui::Button(" OK "))
            {
                m_flash_finished = false;
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::TextWrapped("Device detected with invalid firmware!");
            ImGui::TextWrapped("Would you like to flash the desired firmware %hu.%hhu?",
                constants::DESIRED_FW_VERSION, constants::DESIRED_FW_VARIANT);
            if (ImGui::Button(" Yes "))
                startFirmwareFlash();
            ImGui::SameLine();
            if (ImGui::Button(" Cancel "))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::Update()
{
    // Ensure the frontend for the resolved layout exists before polling, so
    // pollDevice's onDeviceConnected hook has a live frontend to notify.
    ensureFrontend();

    pollDevice();

    // Check safety mode
    if (!safety_mode && CheckIfInSafetyMode())
    {
        safety_mode = true;
        ImGui::OpenPopup("Warning!##SafetyModePopup");
    }
    if (ImGui::BeginPopupModal("Warning!##SafetyModePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Labrador has entered Safety Mode.\nThis can happen if the PSU voltage "
                    "is set too high. \nPlease disconnect and reconnect the device.\n");
        if (ImGui::Button("OK") || !connected)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool uninit_now = CheckIfInUninitialisedMode();
    if (uninit_now)
    {
        uninit_enter_count++;
        uninit_exit_count = 0;
    }
    else
    {
        uninit_exit_count++;
        uninit_enter_count = 0;
    }
    if (!uninitialised_mode && uninit_enter_count >= UNINIT_ENTER_THRESHOLD)
    {
        // NOTE: an automatic USB-port-reset auto-heal was tried here and
        // crashed (resetting with live iso transfers is fragile on macOS);
        // manual replug it is until librador can stop the stream first.
        uninitialised_mode = true;
        ImGui::OpenPopup("Warning!##UninitialisedModePopup");
    }
    if (uninitialised_mode && uninit_exit_count >= UNINIT_EXIT_THRESHOLD)
    {
        uninitialised_mode = false;
    }
    if (ImGui::BeginPopupModal("Warning!##UninitialisedModePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("The device powered up before the OS initialized USB and is stuck in an "
                    "incomplete startup state.\nPlease disconnect and reconnect the device.\n");
        if (ImGui::Button("OK") || !uninitialised_mode)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!connected)
    {
        safety_mode = false;
        uninitialised_mode = false;
    }

    // Draw the whole UI for this form factor and service its widgets.
    m_active_frontend->update(*this);

    // Shared post-layout UI
    if (m_show_demo_windows)
    {
        ImGui::ShowDemoWindow();
        ImPlot::ShowDemoWindow();
    }
    else
        SetGlobalStyle(m_dark_theme);

    if (m_show_debug_console)
        m_debug_console.render(&m_show_debug_console, m_flashing || m_recovering);
    if (m_show_shortcuts && ImGui::Begin("Keyboard Shortcuts", &m_show_shortcuts,
            ImGuiWindowFlags_AlwaysAutoResize))
    {
        struct Row { const char* key; const char* action; };
        const Row rows[] = {
            { "Space", "Run / Stop" },
            { "Up / W", "Increase gain (smaller range)" },
            { "Down / S", "Decrease gain (larger range)" },
            { "F", "Auto-fit both axes" },
            { "1 / 2", "Toggle cursor 1 / 2" },
            { "C / V", "Show / hide channel 1 / 2" },
            { "Esc", "Reconnect device (reset USB)" },
            { "F1", "This help" },
        };
        if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_SizingFixedFit))
        {
            for (const Row& r : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(constants::GRAY_TEXT, "%s", r.key);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.action);
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }
    else if (m_show_shortcuts)
    {
        ImGui::End();
    }

    drawFirmwarePopup();
    drawGobindarPopup();

    // Persist changed settings promptly (throttled) rather than waiting for
    // ShutDown — Android may kill the process without a clean shutdown.
    pushSettings();
    m_active_frontend->saveSettings(m_settings);
    m_settings.saveIfDirty(ImGui::GetTime());

    m_frames++;
}

void App::ShutDown()
{
    // Final settings flush (routine saves already happened in Update)
    pushSettings();
    if (m_active_frontend)
        m_active_frontend->saveSettings(m_settings);
    m_settings.save();

    // Frontend cleanup (turns off the signal generators for instrument UIs)
    if (m_active_frontend)
        m_active_frontend->shutDown(*this);

    if (m_flash_thread.joinable())
        m_flash_thread.join();
    librador_exit();
    g_app = nullptr;
}

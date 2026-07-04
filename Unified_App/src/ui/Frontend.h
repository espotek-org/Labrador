#pragma once

// A Frontend is a complete, self-contained UI for one form factor: it owns its
// own widgets and layout and draws the entire client area each frame. Form
// factors do NOT share widgets — the desktop uses the Monash control widgets,
// mobile uses Brent's compact touch widgets, Pi uses a compact 480p set.
//
// The shared App (session controller) owns everything form-factor-independent:
// SDL/GL lifecycle, the librador connection + polling, firmware/bootloader/
// gobindar recovery flows and their popups, settings persistence, and the
// debug console. It selects one Frontend at startup by platform + resolution
// and drives it each frame.
class App;
class Settings;

class Frontend
{
  public:
    virtual ~Frontend() = default;

    // One-time setup after librador is initialised and shared assets loaded.
    virtual void startUp(App& app) {}

    // Draw the whole UI and service this form factor's widgets for one frame.
    // Called inside an ImGui frame, after the App has polled the device.
    virtual void update(App& app) = 0;

    // Cleanup (e.g. turn off signal generators) before teardown.
    virtual void shutDown(App& app) {}

    // Called by the shared App when a board finishes connecting, so the
    // frontend can run its post-connect widget init (device mode, signal
    // generators, scope gain). The App owns connection detection; the
    // form-factor-specific init lives here.
    virtual void onDeviceConnected(App& app) {}

    // Persist / restore this frontend's own settings keys. The shared App owns
    // the Settings store and the form-factor-independent keys (layout, theme);
    // each frontend contributes its widget-specific keys here.
    virtual void loadSettings(Settings& s) {}
    virtual void saveSettings(Settings& s) {}
};

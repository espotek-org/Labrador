#pragma once

#include <SDL3/SDL.h>
#include "imgui.h"

// Owns SDL init, the window, the GL context, ImGui/ImPlot contexts, fonts and
// the main loop. Derived from the Monash LabraScope AppBase (SDL2) rewritten
// for SDL3 + ImGui 1.92, following the patterns proven in Brent's Android app.
class AppBase
{
  public:
    AppBase(const char* title);
    virtual ~AppBase();

    AppBase(const AppBase&) = delete;
    AppBase& operator=(const AppBase&) = delete;

    void Run();

    // Render this many frames then exit (0 = run until quit). For smoke tests/CI.
    void SetSmokeFrames(int frames) { m_smoke_frames = frames; }

    // Usable content rect (excludes Android system bars) and UI scale — for
    // frontends that lay out the whole client area.
    float contentX() const { return m_content_x; }
    float contentY() const { return m_content_y; }
    float contentW() const { return m_content_w; }
    float contentH() const { return m_content_h; }
    float mainScale() const { return m_main_scale; }

  protected:
    virtual void StartUp() = 0;
    virtual void Update() = 0;  // one ImGui frame
    virtual void ShutDown() = 0;

    void RequestQuit() { m_done = true; }

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    ImVec4 m_clear_color = ImVec4(0.1058f, 0.1137f, 0.1255f, 1.0f);
    float m_main_scale = 1.0f;

    // Usable content rectangle inside the window: on Android this excludes the
    // status bar (top) and navigation bar (bottom); on desktop it is the whole
    // window. Refreshed each frame before Update().
    float m_content_x = 0.0f;
    float m_content_y = 0.0f;
    float m_content_w = 0.0f;
    float m_content_h = 0.0f;

  private:
    void loadFonts();
    bool m_done = false;
    int m_smoke_frames = 0;
};

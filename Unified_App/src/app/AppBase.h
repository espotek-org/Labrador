#pragma once

#include <SDL3/SDL.h>
#include "imgui.h"

#include <string>
#include <vector>

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

    // QA builds (-DLABRADOR_QA=ON): run the test suite headlessly and exit.
    // filter is a Test Engine filter string ("all", "gui", "hw", ...).
    void SetQaRun(const char* filter) { m_qa_run = true; m_qa_filter = filter; }
    bool qaRun() const { return m_qa_run; }
    // Process exit code (non-zero when a headless QA run failed).
    int exitCode() const { return m_exit_code; }

    // Usable content rect (excludes Android system bars) and UI scale — for
    // frontends that lay out the whole client area.
    float contentX() const { return m_content_x; }
    float contentY() const { return m_content_y; }
    float contentW() const { return m_content_w; }
    float contentH() const { return m_content_h; }
    float mainScale() const { return m_main_scale; }

    // User text-size factor (View > Text Size), applied to FontScaleMain
    // each frame. 1.0 = normal.
    float fontScale() const { return m_font_scale; }
    void setFontScale(float s) { m_font_scale = s; }


  protected:
    virtual void StartUp() = 0;
    virtual void Update() = 0;  // one ImGui frame
    virtual void ShutDown() = 0;

    void RequestQuit() { m_done = true; }

    // Fonts loaded at init: Rajdhani (classic + "Modern" CRT themes) and
    // VT323 (the "Retro" CRT themes). Run() picks per frame from the theme.
    ImFont* m_font_default = nullptr;
    ImFont* m_font_retro = nullptr;
    float m_font_scale = 1.0f;

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

  protected:
    int m_exit_code = 0;

  private:
    void loadFonts();
    bool m_done = false;
    int m_smoke_frames = 0;
    bool m_qa_run = false;
    std::string m_qa_filter;
};

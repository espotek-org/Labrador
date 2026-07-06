#include "AppBase.h"

#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "instruments/util.h" // CurrentTheme(), for the per-theme font pick
#include "platform/paths.h"
#include "platform/file_dialog.h"
#include "platform/android_ui.h"
#ifdef LABRADOR_QA
#include "qa/QaSuite.h"
#endif

#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

AppBase::AppBase(const char* title)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    // GL + GLSL version per platform (see docs/PLAN.md renderer matrix)
    const char* glsl_version;
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // Raspberry Pi floor: GLES 2.0 (Pi 3 / VC4 and newer)
    glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // Android
    glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifdef __ANDROID__
    // Scale UI relative to a reference device (Pixel 6a) so widgets are a
    // consistent physical size across phones — Brent's approach, since scaling
    // by SDL's content scale alone doesn't give consistent sizing on Android.
    const float ref_scale = 2.625f; // Pixel 6a content scale
    const float ref_dpi = 428.6f;   // Pixel 6a dpi
    // Brent's mobile widgets are designed for the full Pixel-6a reference
    // scale, so no density fudge (the old <1 factor was a hack for stretching
    // the desktop widgets onto a phone — retired with the mobile frontend).
    m_main_scale = (ref_scale / ref_dpi) * androidGetDpi();
#else
    m_main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
#endif
    if (m_main_scale <= 0.0f)
        m_main_scale = 1.0f;

    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
#ifdef __ANDROID__
    // Fullscreen on the device; size is taken from the display bounds
    SDL_Rect bounds;
    SDL_zero(bounds);
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds);
    m_window = SDL_CreateWindow(title, bounds.w, bounds.h, window_flags);
#else
    // Testing hook: LABRADOR_WINDOW_SIZE=WxH (pixels) pins the window to a
    // target resolution — used by the QA screenshot matrix to verify the
    // compact (800x480) and tablet (1024x768, 1280x720) layouts at the sizes
    // they are designed for instead of the desktop default.
    int win_w = (int)(1200 * m_main_scale);
    int win_h = (int)(800 * m_main_scale);
    if (const char* size_env = SDL_getenv("LABRADOR_WINDOW_SIZE"))
    {
        int w = 0, h = 0;
        if (sscanf(size_env, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240)
        {
            win_w = w;
            win_h = h;
        }
        else
        {
            fprintf(stderr, "LABRADOR_WINDOW_SIZE: expected WxH (e.g. 1024x768), got '%s'\n",
                size_env);
        }
    }
    m_window = SDL_CreateWindow(title, win_w, win_h, window_flags);
#endif
    if (m_window == nullptr)
        throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    m_gl_context = SDL_GL_CreateContext(m_window);
    if (m_gl_context == nullptr)
        throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());

    SDL_GL_MakeCurrent(m_window, m_gl_context);
    SDL_GL_SetSwapInterval(1);
    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

#ifdef __ANDROID__
    // Ask the on-screen keyboard for a numeric-with-decimal/sign layout, since
    // nearly every text field in the app is a number (Brent's approach).
    // Android inputType TYPE_CLASS_NUMBER|FLAG_DECIMAL|FLAG_SIGNED = 2|2002.
    static SDL_PropertiesID ime_props = SDL_CreateProperties();
    SDL_SetNumberProperty(ime_props, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 2 | 2002);
    io.UserData = &ime_props;
#endif

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ChildRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.8f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.0f);
    style.ScaleAllSizes(m_main_scale);
    style.FontScaleDpi = m_main_scale;

    ImGui_ImplSDL3_InitForOpenGL(m_window, m_gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    loadFonts();
}

// Font file bytes, cached for the whole app lifetime: the registry loads
// several base fonts that share the same (large) fallback files, and the
// atlas keeps reading the data lazily, so the fonts reference one cached
// copy each (FontDataOwnedByAtlas = false) instead of per-font duplicates.
static void* fontData(const char* file, int* out_size)
{
    static std::map<std::string, std::vector<unsigned char>>* cache
        = new std::map<std::string, std::vector<unsigned char>>(); // never freed
    auto it = cache->find(file);
    if (it == cache->end())
        it = cache->emplace(file, loadAsset(file)).first;
    *out_size = (int)it->second.size();
    return it->second.data();
}

// Load one base font plus the shared merge fallbacks: arrows/greek (arial),
// check marks (seguisym), and Brent's instrument glyph fonts (waveform
// shapes, delta, DAQ icons). Every selectable font gets the same fallbacks
// so glyphs survive font/theme switches.
static ImFont* loadFontWithFallbacks(const char* file, float size_pixels)
{
    ImGuiIO& io = ImGui::GetIO();

    // Fonts load from memory so the same path works on desktop and from the
    // Android APK's assets.
    ImFontConfig base_config;
    base_config.FontDataOwnedByAtlas = false; // cached in fontData()
    int base_size = 0;
    void* base = fontData(file, &base_size);
    ImFont* font
        = io.Fonts->AddFontFromMemoryTTF(base, base_size, size_pixels, &base_config);
    if (font == nullptr)
        throw std::runtime_error(std::string("Failed to load font: ") + file);

    ImFontConfig config;
    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false;
    // ImGui 1.92 requires a non-zero reference size when a merge font sets a
    // GlyphOffset (the offset is expressed relative to that size). Sizes for
    // the glyph fonts are Brent's tuned values.
    struct MergeFont { const char* file; float size; float y_offset; };
    const MergeFont merge_fonts[] = {
        { "fonts/arial.ttf", 18.0f, 0.0f },
        { "fonts/seguisym.ttf", 18.0f, 0.0f },
        { "fonts/waveform-glyphs3.ttf", 13.0f, 3.0f },
        { "fonts/greek_delta.ttf", 12.0f, 4.5f },
        { "fonts/daq-glyphs.ttf", 16.0f, 5.0f },
    };
    for (const MergeFont& mf : merge_fonts)
    {
        config.GlyphOffset = ImVec2(0.0f, mf.y_offset);
        int size = 0;
        void* data = fontData(mf.file, &size);
        ImFont* merged = io.Fonts->AddFontFromMemoryTTF(data, size, mf.size, &config);
        if (merged == nullptr)
            throw std::runtime_error(std::string("Failed to load font: ") + mf.file);
    }
    return font;
}

void AppBase::loadFonts()
{
    ImGui::GetStyle().FontSizeBase = 18.0f;

    // Two faces (both OFL, licenses alongside the files in assets/fonts):
    // Rajdhani for the classic and "Modern" CRT themes, VT323 (a DEC VT320
    // revival) for the "Retro" CRT themes. Sizes tuned per face.
    m_font_default = loadFontWithFallbacks("fonts/Rajdhani-Medium.ttf", 19.0f);
    m_font_retro = loadFontWithFallbacks("fonts/VT323-Regular.ttf", 21.0f);
}

AppBase::~AppBase()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(m_gl_context);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

// Debug frame dump (LABRADOR_FRAME_DUMP=<path.ppm> with --smoke): writes the
// final frame's framebuffer as binary PPM, for eyeballing layouts headlessly.
static void dumpFramebufferPpm(const char* path, int w, int h)
{
    std::vector<unsigned char> rgba((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "frame dump: cannot open %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) // GL rows are bottom-up
        for (int x = 0; x < w; x++)
            fwrite(&rgba[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
}

void AppBase::Run()
{
    StartUp();

#ifdef LABRADOR_QA
    QaSetup(m_qa_run ? m_qa_filter.c_str() : nullptr);
#endif

    if (m_smoke_frames > 0 || m_qa_run)
        SDL_GL_SetSwapInterval(0);  // don't block on vsync for an invisible CI window

    int frames_rendered = 0;
    while (!m_done)
    {
        if (m_smoke_frames > 0 && frames_rendered++ >= m_smoke_frames)
            m_done = true;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                m_done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                && event.window.windowID == SDL_GetWindowID(m_window))
                m_done = true;
        }

        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Resolve the frame's font before NewFrame so the whole frame uses
        // one font: VT323 for the "Retro" CRT themes, Rajdhani otherwise.
        // CurrentTheme() reflects the theme applied at the end of the
        // previous frame's Update.
        if (m_font_default && m_font_retro)
        {
            const ThemeSpec& t = CurrentTheme();
            ImGui::GetIO().FontDefault
                = (t.retro && t.pixelFont) ? m_font_retro : m_font_default;
        }
        // Accessibility text-size factor (View > Text Size). Layout widths
        // are CalcTextSize-driven, so everything reflows with the text.
        ImGui::GetStyle().FontScaleMain = m_font_scale;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Usable content rect: full display minus Android system bars.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
#ifdef __ANDROID__
        const float status_h = (float)androidStatusBarHeight();
        const float nav_h = (float)androidNavigationBarHeight();
        m_content_x = 0.0f;
        m_content_y = status_h;
        m_content_w = display.x;
        m_content_h = display.y - status_h - nav_h;
#else
        m_content_x = 0.0f;
        m_content_y = 0.0f;
        m_content_w = display.x;
        m_content_h = display.y;
#endif

        PumpFileDialogResults();
        Update();
#ifdef LABRADOR_QA
        QaDrawUI();
#endif

        ImGui::Render();
        int display_w, display_h;
        SDL_GetWindowSizeInPixels(m_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(m_clear_color.x * m_clear_color.w, m_clear_color.y * m_clear_color.w,
            m_clear_color.z * m_clear_color.w, m_clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (m_smoke_frames > 0 && m_done)
            if (const char* dump = SDL_getenv("LABRADOR_FRAME_DUMP"))
                dumpFramebufferPpm(dump, display_w, display_h);
#ifdef LABRADOR_QA
        // Prediction-QA: a running test asked for this frame to be captured.
        if (const char* cap = QaConsumeFrameDump())
            dumpFramebufferPpm(cap, display_w, display_h);
#endif
        SDL_GL_SwapWindow(m_window);

#ifdef LABRADOR_QA
        QaPostSwap();
        if (m_qa_run && QaFinished())
        {
            m_exit_code = QaReportAndExitCode();
            m_done = true;
        }
#endif
    }

#ifdef LABRADOR_QA
    QaShutdown();
#endif

    ShutDown();
}

// Dear ImGui: standalone example application for SDL3 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "librador.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "implot.h"
#include "imgui.h"
#include "settings_panel.h"
#include "ui_tile.h"
#include "sig_gen_ui.h"
#include "inputs_ui.h"
#include "trigger_ui.h"
#include "virtual_transform_ui.h"
#include "psu_ui.h"
#include "logic_decode_ui.h"
#include "plot_ui.h"
#include "custom_imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
// #include "SDL_android.h"
#include <SDL3/SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include "imgui_internal.h"
#include <stdlib.h>
#include <chrono>
#include <SDL3/SDL_events.h>


// Main code
int main(int, char**)
{
    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Rect bounds;
    SDL_zero(bounds);
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &bounds);

    float pixel_6a_main_scale = 2.625;
    float pixel_6a_dpi = 428.6;

    JNIEnv *env = (JNIEnv *) SDL_GetAndroidJNIEnv();
    jobject MainActivityObject = (jobject) SDL_GetAndroidActivity();
    jclass MainActivity(env->GetObjectClass(MainActivityObject));
    jmethodID getDpiID = env->GetMethodID(MainActivity, "getDpi", "()F");
    float dpi = (float) env->CallFloatMethod(MainActivityObject,getDpiID);
    LOGI("dpi: %.2f", dpi);
    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("main window", (int)bounds.w, (int)bounds.h, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    SDL_PropertiesID propsIme = SDL_CreateProperties(); // for allowing specification of keyboard type (numeric, alpha, ...)
    SDL_SetNumberProperty(propsIme, SDL_PROP_TEXTINPUT_ANDROID_INPUTTYPE_NUMBER, 2);
    io.UserData = &propsIme;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
//     style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again). 
//     style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    style.ScaleAllSizes( (pixel_6a_main_scale / pixel_6a_dpi) * dpi); // brentfpage : not scaling by a given device's main_scale because doing so doesn't actually bring about consistent sizing across devices.  
    style.FontScaleDpi = (pixel_6a_main_scale / pixel_6a_dpi) * dpi;        // brentfpage : same for the font sizes
    
    style.FontSizeBase = 19.f;
    style.WindowPadding = ImVec2(style.WindowPadding.x/2,style.WindowPadding.y/2);

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will call AddFontDefault() to select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    ImFont* defaultFont = io.Fonts->AddFontDefault();

    // for accessing android app resources
    jfieldID asset_manager_id = env->GetFieldID(MainActivity, "mgr", "Landroid/content/res/AssetManager;");
    jobject mgr_java = (jobject)env->GetObjectField(MainActivityObject, asset_manager_id);
    AAssetManager * mgr = AAssetManager_fromJava(env, mgr_java);

    ImFontConfig config;
    config.MergeMode = true;
    config.FontDataOwnedByAtlas = false; // prevents imperceptible crash when the app is closed
//     https://stackoverflow.com/a/13317651/3474552

    float glyph_y_offsets[2] = {3.f, 4.5f};
    char buf[2][2048];
    int fi = 0;
    for (const char* filename: {"font/waveform-glyphs3.ttf","font/greek_delta.ttf"}) {
        config.GlyphOffset = { 0.f, glyph_y_offsets[fi] };
        AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_STREAMING);
        int nb_read = 0;
        nb_read = AAsset_read(asset, buf[fi], 2048);
        ImFont* new_font = io.Fonts->AddFontFromMemoryTTF(buf[fi], nb_read, 13.f, &config);
        AAsset_close(asset);
        fi++;
    }

    jmethodID getStatusBarHeightID = env->GetMethodID(MainActivity, "getStatusBarHeight", "()I");
    jmethodID getNavigationBarHeightID = env->GetMethodID(MainActivity, "getNavigationBarHeight", "()I");
    jmethodID getScreenWidth = env->GetMethodID(MainActivity, "getScreenWidth", "()I");
    jmethodID getScreenHeight = env->GetMethodID(MainActivity, "getScreenHeight", "()I");

    int sw = (int) env->CallIntMethod(MainActivityObject,getScreenWidth);
    int sh = (int) env->CallIntMethod(MainActivityObject,getScreenHeight);
    int sbh = (int) env->CallIntMethod(MainActivityObject,getStatusBarHeightID);
    int nbh = (int) env->CallIntMethod(MainActivityObject,getNavigationBarHeightID);
    LOGI("screen width: %d", sw);
    LOGI("screen height: %d", sh);
    LOGI("status bar beight: %d", sbh);
    LOGI("navigation bar beight: %d", nbh);
    
    // Our state
    bool show_mainwindow = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    
    virtual_transform_ui.is_visible = true;
    virtual_transform_ui.is_expanded = false;
    virtual_transform_ui.next_is_expanded = false;
    psu_ui.is_expanded = false;
    psu_ui.is_visible = false;
    plotUI plot_ui = plotUI();

    // Main loop
    bool done = false;
    bool iso_thread_active = false;
    bool need_board_init = true;
    while (!done)
    {

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // will intentionally be zero sometimes; see MainActivity
        int statusBarHeight = (int) env->CallIntMethod(MainActivityObject,getStatusBarHeightID);
        int navigationBarHeight = (int) env->CallIntMethod(MainActivityObject,getNavigationBarHeightID);

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

// important to have this iso_thread_active check after the new frame starts.  Otherwise, (board connected -> user puts phone to sleep -> user unplugs board -> user wakes phone) leads to a crash.  The crash arises from the librador_get_(analog/digital)_data block below thinking iso_thread_active=true when it's not.
        iso_thread_active = librador_iso_thread_is_active();
        if(!iso_thread_active) {
            need_board_init = true;
        }
        if(need_board_init && iso_thread_active) {
            inputs_ui.update_device_mode();
            sig_gen_ui.usb_send_data(1);
            sig_gen_ui.usb_send_data(2);
            psu_ui.usb_send_data();
            librador_set_oscilloscope_gain(4.);
            need_board_init = false;
        }

        ImGuiIO& io = ImGui::GetIO();

        static bool landscape = io.DisplaySize.y < io.DisplaySize.x;
        bool new_landscape = io.DisplaySize.y < io.DisplaySize.x;
        bool orientation_changed = (landscape != new_landscape);
        landscape = new_landscape;

        plot_ui.recompute_x_bounds(inputs_ui.changed_since_last(), inputs_ui.mode);
        logic_decode_ui.update(&inputs_ui);

        ImGui::SetNextWindowPos(ImVec2(0.f,statusBarHeight));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x,io.DisplaySize.y - statusBarHeight - navigationBarHeight));

        ImGuiStyle& style = ImGui::GetStyle();

        bool screen_keyboard_shown = SDL_ScreenKeyboardShown(window);
        ImGui::Begin("MainWindow",
                     &show_mainwindow,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | (screen_keyboard_shown ? ImGuiWindowFlags_NoMouseInputs : 0));   
        float data_width;
        float data_height;
        do_settings_panel_layout(&data_width, &data_height, landscape, io.DisplaySize.y - statusBarHeight - navigationBarHeight - 2 * style.WindowPadding.y, dpi, pixel_6a_dpi);

        ImGui::BeginChild("data",ImVec2(data_width, data_height), 0, (screen_keyboard_shown ? ImGuiWindowFlags_NoMouseInputs : 0));
        {
            if(logic_decode_ui.decoding_on()) {
                logic_decode_ui.draw_console(data_width);
            }
                
            plot_ui.draw(iso_thread_active, inputs_ui.mode, inputs_ui.ch_enabled(1), inputs_ui.ch_enabled(2), data_width, 0.);

        }
        ImVec2 dataWindowBottomLeft = ImGui::GetWindowPos() + ImVec2(0.f,ImGui::GetWindowSize().y);
        ImVec2 dataWindowBottomRight = ImGui::GetWindowPos() + ImGui::GetWindowSize();
        ImGui::EndChild();
        if(landscape) {
            ImGui::SameLine();
        }

        draw_settings_panel(landscape, screen_keyboard_shown);
        draw_collapse_button(landscape, dataWindowBottomLeft, dataWindowBottomRight);
        draw_selector_popup(landscape, orientation_changed);

        ImGui::End();

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyProperties(propsIme);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

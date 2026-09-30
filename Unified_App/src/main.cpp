#include "app/App.h"
#include "platform/android_hooks.h"

#ifndef __ANDROID__
// Desktop: SDL_main.h provides the platform entry shim. On Android, SDL's Java
// glue (SDLActivity) dlsym's the exported "main" directly, so including this
// header there would rename our entry to SDL_main and SDL couldn't find it.
#include <SDL3/SDL_main.h>
#endif
#include <string>
#ifdef _WIN32
#include <cstdio>
#include <cstdlib>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// labrador.exe is a GUI-subsystem executable (no console window when
// launched from Explorer or the Start menu).  Started from a terminal, attach
// to that terminal so --qa/--smoke output and the log mirror still print
// there; LABRADOR_CONSOLE=1 opens a console for an Explorer launch instead.
static void attachParentConsole()
{
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!attached && std::getenv("LABRADOR_CONSOLE"))
        attached = AllocConsole() != 0;
    if (!attached)
        return;
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
    (void)freopen("CONIN$", "r", stdin);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
#endif
int main(int argc, char** argv)
{
#ifdef __ANDROID__
    librador_register_android_hooks();
#endif
#ifdef _WIN32
    attachParentConsole();
#endif
    App app;
    for (int i = 1; i < argc; i++)
    {
        const std::string arg(argv[i]);
        if (arg == "--smoke")
            app.SetSmokeFrames(60);
#ifdef LABRADOR_QA
        // Headless UI test run (QA builds): --qa runs everything, or
        // --qa=<filter> for a Test Engine filter like "gui" or "hw".
        else if (arg == "--qa")
            app.SetQaRun("all");
        else if (arg.rfind("--qa=", 0) == 0)
            app.SetQaRun(arg.c_str() + 5);
#endif
    }
    app.Run();
    return app.exitCode();
}

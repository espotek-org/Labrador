#include "app/App.h"
#include "platform/android_hooks.h"

#ifndef __ANDROID__
// Desktop: SDL_main.h provides the platform entry shim. On Android, SDL's Java
// glue (SDLActivity) dlsym's the exported "main" directly, so including this
// header there would rename our entry to SDL_main and SDL couldn't find it.
#include <SDL3/SDL_main.h>
#endif
#include <string>

#ifdef __ANDROID__
extern "C" __attribute__((visibility("default")))
#endif
int main(int argc, char** argv)
{
#ifdef __ANDROID__
    librador_register_android_hooks();
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

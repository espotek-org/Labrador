#include "app/App.h"
#include "platform/android_hooks.h"
#ifndef __ANDROID__
#include "autoqa/AutoQa.h"
#include "autoqa/Plan.h"

#include "JsonWriter.h"

#include <sstream>
#endif

#ifndef __ANDROID__
// Desktop: SDL_main.h provides the platform entry shim. On Android, SDL's Java
// glue (SDLActivity) dlsym's the exported "main" directly, so including this
// header there would rename our entry to SDL_main and SDL couldn't find it.
#include <SDL3/SDL_main.h>
#endif
#include <cstdio>
#include <memory>
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

// Whether a standard handle already leads to a file or a pipe (as with
// `labrador.exe > log 2>&1`) rather than to a console or nowhere.
static bool isRedirected(DWORD which)
{
    const HANDLE h = GetStdHandle(which);
    if (h == nullptr || h == INVALID_HANDLE_VALUE)
        return false;
    const DWORD type = GetFileType(h);
    return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
}

// labrador.exe is a GUI-subsystem executable (no console window when
// launched from Explorer or the Start menu).  Started from a terminal, attach
// to that terminal so --qa/--smoke output and the log mirror still print
// there; LABRADOR_CONSOLE=1 opens a console for an Explorer launch instead.
// A stream the caller redirected to a file or pipe stays there: reopening it
// on the console left `labrador.exe --auto-qa <dir> > log 2>&1` with an empty
// log, and the QA tester with nothing to go on when the app failed.
static void attachParentConsole()
{
    const bool out_redirected = isRedirected(STD_OUTPUT_HANDLE);
    const bool err_redirected = isRedirected(STD_ERROR_HANDLE);
    const bool in_redirected = isRedirected(STD_INPUT_HANDLE);
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!attached && std::getenv("LABRADOR_CONSOLE"))
        attached = AllocConsole() != 0;
    if (attached)
    {
        if (!out_redirected)
            (void)freopen("CONOUT$", "w", stdout);
        if (!err_redirected)
            (void)freopen("CONOUT$", "w", stderr);
        if (!in_redirected)
            (void)freopen("CONIN$", "r", stdin);
    }
    // Unbuffered either way, so nothing is lost if the app dies.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

#ifndef __ANDROID__
// Prints the plan `--auto-qa-list` asked for as one JSON object, through the same stdout
// as every other line the app prints (on Windows, the one attachParentConsole set up).
static void printPlan(const autoqa::Plan& plan)
{
    std::ostringstream text;
    JsonWriter json(text);
    json.beginObject();
    json.field("schema_version", 3);
    autoqa::writePlanJson(json, plan);
    json.endObject();
    json.finish();
    std::fputs(text.str().c_str(), stdout);
    std::fflush(stdout);
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
    // Every argument must be understood: an unknown one is an error, so a caller
    // can tell a build that lacks an option (exit 2) from one that runs it.
    bool smoke = false;
#ifndef __ANDROID__
    std::string auto_qa_dir;
    bool auto_qa_list = false;
    autoqa::Plan auto_qa_plan;
#endif
#ifdef LABRADOR_QA
    std::string qa_filter;
#endif
#ifndef __ANDROID__
    // Usage errors (autoqa::UsageError) are found here, before an output directory,
    // progress.log or a window exists: one line on stderr and exit 2.
    try
    {
        autoqa::Options qa_options;
        bool level_given = false, tests_given = false, repeat_given = false;
        // The value after option argv[i], which must be there.
        const auto valueOf = [&](int& i) -> std::string {
            if (i + 1 >= argc)
                throw autoqa::UsageError(std::string(argv[i]) + " needs a value");
            return argv[++i];
        };
        const auto once = [](bool& given, const char* option) {
            if (given)
                throw autoqa::UsageError(std::string(option) + " is given twice");
            given = true;
        };
        bool auto_qa_given = false;
#endif
        for (int i = 1; i < argc; i++)
        {
            const std::string arg(argv[i]);
            if (arg == "--smoke") // render 60 frames and exit (CI smoke test)
                smoke = true;
#ifndef __ANDROID__
            else if (arg == "--auto-qa") // the QA, report in the directory given (docs/auto-qa.md)
            {
                once(auto_qa_given, "--auto-qa");
                if (i + 1 >= argc)
                    throw autoqa::UsageError("--auto-qa needs an output directory");
                auto_qa_dir = argv[++i];
            }
            else if (arg == "--auto-qa-list") // print the plan the options would run, as JSON
                once(auto_qa_list, "--auto-qa-list");
            else if (arg == "--auto-qa-level")
            {
                once(level_given, "--auto-qa-level");
                qa_options.level = autoqa::parseLevel(valueOf(i));
            }
            else if (arg == "--auto-qa-tests")
            {
                once(tests_given, "--auto-qa-tests");
                qa_options.selection = autoqa::parseSelection(valueOf(i));
            }
            else if (arg == "--auto-qa-repeat")
            {
                once(repeat_given, "--auto-qa-repeat");
                qa_options.repeat = autoqa::parseRepeat(valueOf(i));
            }
#endif
#ifdef LABRADOR_QA
            // Headless UI test run (QA builds): --qa runs everything, or
            // --qa=<filter> for a Test Engine filter like "gui" or "hw".
            else if (arg == "--qa")
                qa_filter = "all";
            else if (arg.rfind("--qa=", 0) == 0)
                qa_filter = arg.substr(5);
#endif
#ifdef __APPLE__
            // Finder adds a process serial number when it launches the bundle on
            // older macOS releases (the app supports 10.13).
            else if (arg.rfind("-psn_", 0) == 0)
                continue;
#endif
            else
            {
                fprintf(stderr, "labrador: unknown option '%s'\n", arg.c_str());
                return 2;
            }
        }
#ifndef __ANDROID__
        if (auto_qa_given && auto_qa_list)
            throw autoqa::UsageError("--auto-qa and --auto-qa-list cannot be used together");
        if ((level_given || tests_given || repeat_given) && !auto_qa_given && !auto_qa_list)
            throw autoqa::UsageError(std::string(level_given ? "--auto-qa-level" : tests_given ? "--auto-qa-tests"
                                                                                               : "--auto-qa-repeat") +
                                     " is only valid with --auto-qa or --auto-qa-list");
        if (auto_qa_given || auto_qa_list)
            auto_qa_plan = autoqa::makePlan(qa_options);
    }
    catch (const autoqa::UsageError& e)
    {
        fprintf(stderr, "labrador: %s\n", e.what());
        return 2;
    }
    if (auto_qa_list)
    {
        printPlan(auto_qa_plan);
        return 0;
    }

    // progress.log exists before any window does: the QA server watches for it.
    std::unique_ptr<autoqa::AutoQa> auto_qa;
    if (!auto_qa_dir.empty())
        auto_qa = std::make_unique<autoqa::AutoQa>(auto_qa_dir, argv[0], std::move(auto_qa_plan));
#endif
    App app;
    if (smoke)
        app.SetSmokeFrames(60);
#ifdef LABRADOR_QA
    if (!qa_filter.empty())
        app.SetQaRun(qa_filter.c_str());
#endif
#ifndef __ANDROID__
    if (auto_qa)
        app.attachAutoQa(auto_qa.get());
#endif
    app.Run();
    return app.exitCode();
}

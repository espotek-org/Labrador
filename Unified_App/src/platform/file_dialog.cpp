#include "file_dialog.h"

#include <SDL3/SDL.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// SDL may fire dialog callbacks on another thread; queue the results and
// deliver them on the main thread via PumpFileDialogResults().
namespace
{
struct PendingResult
{
    std::function<void(const char*)> on_done;
    std::string path;
    bool has_path;
};

std::mutex g_mutex;
std::vector<PendingResult> g_results;

struct DialogCtx
{
    std::function<void(const char*)> on_done;
    std::string filter_name;     // keep the filter strings alive for SDL
    std::string filter_pattern;
    SDL_DialogFileFilter filter;
};

void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int)
{
    DialogCtx* ctx = static_cast<DialogCtx*>(userdata);
    PendingResult result;
    result.on_done = std::move(ctx->on_done);
    result.has_path = (filelist != nullptr && filelist[0] != nullptr);
    if (result.has_path)
        result.path = filelist[0];
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_results.push_back(std::move(result));
    }
    delete ctx;
}
} // namespace

void ShowSaveFileDialog(const char* extension, std::function<void(const char*)> on_done)
{
    DialogCtx* ctx = new DialogCtx;
    ctx->on_done = std::move(on_done);
    ctx->filter_name = std::string(extension) + " files";
    ctx->filter_pattern = extension;
    ctx->filter = SDL_DialogFileFilter { ctx->filter_name.c_str(), ctx->filter_pattern.c_str() };
    SDL_ShowSaveFileDialog(dialogCallback, ctx, nullptr, &ctx->filter, 1, nullptr);
}

void ShowOpenFileDialog(const char* extension, std::function<void(const char*)> on_done)
{
    DialogCtx* ctx = new DialogCtx;
    ctx->on_done = std::move(on_done);
    ctx->filter_name = std::string(extension) + " files";
    ctx->filter_pattern = extension;
    ctx->filter = SDL_DialogFileFilter { ctx->filter_name.c_str(), ctx->filter_pattern.c_str() };
    // allow_many = false: single-selection, so dialogCallback's filelist[0] is
    // the one chosen path (same result shape as the save dialog).
    SDL_ShowOpenFileDialog(dialogCallback, ctx, nullptr, &ctx->filter, 1, nullptr, false);
}

void PumpFileDialogResults()
{
    std::vector<PendingResult> ready;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ready.swap(g_results);
    }
    for (PendingResult& r : ready)
        r.on_done(r.has_path ? r.path.c_str() : nullptr);
}

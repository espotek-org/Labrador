#include "paths.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>

std::string getResourcePath(const std::string& relative)
{
    // SDL_GetBasePath: exe dir on Windows/Linux, Contents/Resources inside a
    // macOS .app bundle. Dev builds get assets/ copied next to the binary by CMake.
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        throw std::runtime_error(std::string("SDL_GetBasePath failed: ") + SDL_GetError());

    std::string path = std::string(base) + "assets/" + relative;
    if (!std::filesystem::exists(path))
        throw std::runtime_error("Missing bundled asset: " + path);
    return path;
}

std::vector<unsigned char> loadAsset(const std::string& relative)
{
#ifdef __ANDROID__
    // Relative paths make SDL_IOFromFile read from the APK's assets/
    std::string path = relative;
#else
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
        throw std::runtime_error(std::string("SDL_GetBasePath failed: ") + SDL_GetError());
    std::string path = std::string(base) + "assets/" + relative;
#endif

    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr)
        throw std::runtime_error(
            "Missing bundled asset: " + path + " (" + SDL_GetError() + ")");
    std::vector<unsigned char> buf(
        static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
    SDL_free(data);
    return buf;
}

std::string getPrefPath()
{
    char* pref = SDL_GetPrefPath("EspoTek", "Labrador");
    if (pref == nullptr)
        throw std::runtime_error(std::string("SDL_GetPrefPath failed: ") + SDL_GetError());
    std::string result(pref);
    SDL_free(pref);
    return result;
}

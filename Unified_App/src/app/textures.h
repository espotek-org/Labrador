#pragma once

// stb_image -> GL texture helpers (moved out of the Monash AppBase).
// Used for the pinout PNGs shown in widget help sections.
#include <cstddef>

bool LoadTextureFromMemory(const void* data, size_t data_size, unsigned int* out_texture,
    int* out_width, int* out_height);
bool LoadTextureFromFile(
    const char* file_name, unsigned int* out_texture, int* out_width, int* out_height);

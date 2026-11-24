#pragma once

#include <string>
#include <vector>

namespace autoqa
{
// Writes 8-bit RGB pixels, rows top to bottom, as a PNG. Throws std::runtime_error
// if the file cannot be written.
void writePngRgb(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb);
} // namespace autoqa

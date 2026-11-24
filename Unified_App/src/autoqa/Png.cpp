// PNG output for --auto-qa: plots and screenshots.
// deps/stb_image_write.h is v1.19 and deps/stb_easy_font.h v1.2, both from
// https://github.com/nothings/stb at commit 6b5799bae8a1fe3792691ef4b72ae26434c3e799
// (fetched 2026-11-23), public domain / MIT.
#include "autoqa/Png.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#include <stdexcept>

namespace autoqa
{
void writePngRgb(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb)
{
    if (rgb.size() != static_cast<size_t>(width) * height * 3)
        throw std::logic_error("writePngRgb: buffer size does not match " + std::to_string(width) +
                               "x" + std::to_string(height));
    if (!stbi_write_png(path.c_str(), width, height, 3, rgb.data(), width * 3))
        throw std::runtime_error("cannot write " + path);
}
} // namespace autoqa

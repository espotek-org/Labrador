#include "autoqa/Plot.h"

#include "autoqa/Png.h"

#include "stb_easy_font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace autoqa
{
namespace
{
constexpr int TEXT_SCALE = 2; // stb_easy_font glyphs are ~7 px tall; draw them twice the size

class Canvas
{
  public:
    Canvas(int w, int h) : m_w(w), m_h(h), m_px(static_cast<size_t>(w) * h * 3, 255) {}

    void dot(int x, int y, uint32_t rgb)
    {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h)
            return;
        unsigned char* p = &m_px[(static_cast<size_t>(y) * m_w + x) * 3];
        p[0] = (rgb >> 16) & 0xff;
        p[1] = (rgb >> 8) & 0xff;
        p[2] = rgb & 0xff;
    }

    void rect(int x0, int y0, int x1, int y1, uint32_t rgb)
    {
        for (int y = std::max(0, y0); y < std::min(m_h, y1); y++)
            for (int x = std::max(0, x0); x < std::min(m_w, x1); x++)
                dot(x, y, rgb);
    }

    // A line `width` pixels thick; dashed draws 8 on, 6 off.
    void line(double x0, double y0, double x1, double y1, uint32_t rgb, int width, bool dashed)
    {
        const double dx = x1 - x0, dy = y1 - y0;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
        const int half = width / 2;
        for (int i = 0; i <= steps; i++)
        {
            if (dashed && (m_dash++ % 14) >= 8)
                continue;
            const int x = static_cast<int>(std::lround(x0 + dx * i / steps));
            const int y = static_cast<int>(std::lround(y0 + dy * i / steps));
            rect(x - half, y - half, x - half + width, y - half + width, rgb);
        }
    }

    int textWidth(const std::string& text) const
    {
        return stb_easy_font_width(const_cast<char*>(text.c_str())) * TEXT_SCALE;
    }

    void text(int x, int y, const std::string& text, uint32_t rgb)
    {
        thread_local static char buffer[200000];
        const int quads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr, buffer,
                                              sizeof buffer);
        for (int q = 0; q < quads; q++)
        {
            const float* v = reinterpret_cast<const float*>(buffer + q * 4 * 16);
            // Each vertex is 16 bytes: x, y, z floats and a colour; corners 0 and 2.
            const float qx0 = v[0], qy0 = v[1], qx1 = v[8], qy1 = v[9];
            rect(x + static_cast<int>(qx0 * TEXT_SCALE), y + static_cast<int>(qy0 * TEXT_SCALE),
                 x + static_cast<int>(qx1 * TEXT_SCALE), y + static_cast<int>(qy1 * TEXT_SCALE), rgb);
        }
    }

    int width() const { return m_w; }
    int height() const { return m_h; }
    const std::vector<unsigned char>& pixels() const { return m_px; }

  private:
    int m_w, m_h;
    std::vector<unsigned char> m_px;
    int m_dash = 0;
};

// Round-number ticks spanning [lo, hi].
std::vector<double> ticks(double lo, double hi)
{
    const double span = hi - lo;
    const double raw = span / 6;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * mag)
        {
            step = m * mag;
            break;
        }
    std::vector<double> out;
    for (double t = std::ceil(lo / step) * step; t <= hi + step * 1e-9; t += step)
        out.push_back(std::fabs(t) < step * 1e-9 ? 0.0 : t);
    return out;
}

std::string number(double v)
{
    char text[32];
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}
} // namespace

Plot::Plot(std::string title, std::string x_label, std::string y_label)
    : m_title(std::move(title)), m_x_label(std::move(x_label)), m_y_label(std::move(y_label))
{
}

void Plot::save(const std::string& path, int width, int height) const
{
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (const Series& s : m_series)
    {
        if (s.x.size() != s.y.size())
            throw std::logic_error("Plot: series '" + s.label + "' has mismatched x and y");
        for (size_t i = 0; i < s.x.size(); i++)
        {
            x0 = std::min(x0, s.x[i]);
            x1 = std::max(x1, s.x[i]);
            y0 = std::min(y0, s.y[i]);
            y1 = std::max(y1, s.y[i]);
        }
    }
    for (const Ref& r : m_hlines)
    {
        y0 = std::min(y0, r.at);
        y1 = std::max(y1, r.at);
    }
    if (!(x1 > x0))
        throw std::logic_error("Plot '" + m_title + "' has no x range to draw");
    if (!(y1 > y0))
    {
        y0 -= 0.5;
        y1 += 0.5;
    }
    const double pad = (y1 - y0) * 0.08;
    y0 -= pad;
    y1 += pad;

    Canvas c(width, height);
    const int left = 80, right = width - 20, top = 48, bottom = height - 50;
    auto px = [&](double x) { return left + (x - x0) / (x1 - x0) * (right - left); };
    auto py = [&](double y) { return bottom - (y - y0) / (y1 - y0) * (bottom - top); };

    for (const double t : ticks(x0, x1))
    {
        c.line(px(t), top, px(t), bottom, 0xe6e6e6, 1, false);
        const std::string label = number(t);
        c.text(static_cast<int>(px(t)) - c.textWidth(label) / 2, bottom + 8, label, 0x333333);
    }
    for (const double t : ticks(y0, y1))
    {
        c.line(left, py(t), right, py(t), 0xe6e6e6, 1, false);
        const std::string label = number(t);
        c.text(left - 8 - c.textWidth(label), static_cast<int>(py(t)) - 6, label, 0x333333);
    }
    c.line(left, bottom, right, bottom, 0x333333, 1, false);
    c.line(left, top, left, bottom, 0x333333, 1, false);
    for (const Ref& r : m_hlines)
    {
        c.line(left, py(r.at), right, py(r.at), r.rgb, 1, true);
        c.text(left + 6, static_cast<int>(py(r.at)) - 18, r.label, r.rgb);
    }
    for (const Ref& r : m_vlines)
    {
        c.line(px(r.at), top, px(r.at), bottom, r.rgb, 1, true);
        c.text(static_cast<int>(px(r.at)) + 4, top + 4, r.label, r.rgb);
    }
    for (const Series& s : m_series)
    {
        if (s.markers)
        {
            for (size_t i = 0; i < s.x.size(); i++)
                c.rect(static_cast<int>(px(s.x[i])) - 3, static_cast<int>(py(s.y[i])) - 3,
                       static_cast<int>(px(s.x[i])) + 4, static_cast<int>(py(s.y[i])) + 4, s.rgb);
            continue;
        }
        for (size_t i = 1; i < s.x.size(); i++)
            c.line(px(s.x[i - 1]), py(s.y[i - 1]), px(s.x[i]), py(s.y[i]), s.rgb, s.width, s.dashed);
    }

    c.text(left, 6, m_title, 0x111111);
    c.text((left + right) / 2 - c.textWidth(m_x_label) / 2, height - 22, m_x_label, 0x333333);
    c.text(8, 26, m_y_label, 0x333333);
    // Legend, top right.
    int ly = top + 8;
    for (const Series& s : m_series)
    {
        if (s.label.empty())
            continue;
        const int lx = right - 16 - c.textWidth(s.label) - 30;
        c.rect(lx - 6, ly - 4, right - 8, ly + 18, 0xffffff);
        c.line(lx, ly + 7, lx + 22, ly + 7, s.rgb, 3, s.dashed);
        c.text(lx + 28, ly, s.label, 0x222222);
        ly += 22;
    }
    writePngRgb(path, width, height, c.pixels());
}
} // namespace autoqa

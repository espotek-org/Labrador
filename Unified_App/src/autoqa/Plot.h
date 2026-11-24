#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace autoqa
{
// A small software-rendered line chart saved as PNG, for the report's plots:
// works on the hardware thread without the GUI. Axes get round-number ticks;
// text uses stb_easy_font.
class Plot
{
  public:
    struct Series
    {
        std::vector<double> x;
        std::vector<double> y;
        uint32_t rgb = 0x1f77b4;
        int width = 2;       // pixels
        bool dashed = false;
        bool markers = false; // squares at the points instead of a line
        std::string label;
    };

    Plot(std::string title, std::string x_label, std::string y_label);

    void add(Series series) { m_series.push_back(std::move(series)); }
    // A horizontal or vertical reference line with a short label.
    void hline(double y, uint32_t rgb, std::string label) { m_hlines.push_back({y, rgb, std::move(label)}); }
    void vline(double x, uint32_t rgb, std::string label) { m_vlines.push_back({x, rgb, std::move(label)}); }

    // Renders and writes the PNG; throws std::runtime_error on failure.
    void save(const std::string& path, int width = 1000, int height = 420) const;

    static constexpr uint32_t BLUE = 0x1f77b4;
    static constexpr uint32_t RED = 0xd62728;
    static constexpr uint32_t GREEN = 0x2ca02c;
    static constexpr uint32_t GREY = 0x9aa5b1;
    static constexpr uint32_t ORANGE = 0xff7f0e;

  private:
    struct Ref
    {
        double at;
        uint32_t rgb;
        std::string label;
    };
    std::string m_title, m_x_label, m_y_label;
    std::vector<Series> m_series;
    std::vector<Ref> m_hlines, m_vlines;
};
} // namespace autoqa

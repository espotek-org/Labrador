#include "WaveFit.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
constexpr double PI = 3.14159265358979323846;

struct Levels
{
    double a = 0; // y ~ a + b * shape
    double b = 0;
    double sse = 0;
};

// Least squares of y against the shape at the given times.
Levels solveLevels(SignalSpec::Wave wave, double f, double phase, const std::vector<double>& t,
                   const std::vector<double>& y)
{
    const size_t n = y.size();
    double sx = 0, sxx = 0, sy = 0, sxy = 0, syy = 0;
    for (size_t i = 0; i < n; i++)
    {
        const double x = idealShape(wave, f * t[i] + phase);
        sx += x;
        sxx += x * x;
        sy += y[i];
        sxy += x * y[i];
        syy += y[i] * y[i];
    }
    const double nn = static_cast<double>(n);
    const double det = nn * sxx - sx * sx;
    Levels l;
    if (det <= 0)
    {
        l.sse = std::numeric_limits<double>::infinity();
        return l; // the shape is constant over these samples
    }
    l.b = (nn * sxy - sx * sy) / det;
    l.a = (sy - l.b * sx) / nn;
    // Sum of squared residuals from the sums, so the window is walked once.
    l.sse = syy - 2 * l.a * sy - 2 * l.b * sxy + nn * l.a * l.a + 2 * l.a * l.b * sx +
            l.b * l.b * sxx;
    if (l.sse < 0)
        l.sse = 0;
    return l;
}

// Only an upright shape counts (b > 0): for symmetric shapes an inverted fit
// half a cycle away matches equally well and would swap low and high.
double score(const Levels& l)
{
    return l.b > 0 ? l.sse : std::numeric_limits<double>::infinity();
}
} // namespace

double idealShape(SignalSpec::Wave wave, double phase)
{
    const double x = phase - std::floor(phase);
    switch (wave)
    {
    case SignalSpec::Wave::Sine: return (1.0 + std::sin(2 * PI * x)) / 2.0;
    case SignalSpec::Wave::Square: return x > 0.5 ? 1.0 : 0.0;
    case SignalSpec::Wave::Sawtooth: return x;
    case SignalSpec::Wave::Triangle: return x <= 0.5 ? 2 * x : 2 * (1 - x);
    default: throw std::logic_error("no ideal shape for this wave");
    }
}

double WaveFit::at(SignalSpec::Wave wave, double t) const
{
    return low_v + vpp() * idealShape(wave, frequency_hz * t + phase);
}

WaveFit fitWave(SignalSpec::Wave wave, double frequency_hz, double frequency_span,
                const std::vector<double>& y, double sample_rate_hz, double zero_index)
{
    if (y.size() < 10)
        throw std::runtime_error("too few samples to fit a waveform");
    const size_t n = y.size();
    const double t_mid = (static_cast<double>(n / 2) - zero_index) / sample_rate_hz;
    const size_t step = std::max<size_t>(1, n / 20000);
    std::vector<double> tc, yc; // decimated, time from the window's middle
    for (size_t i = 0; i < n; i += step)
    {
        tc.push_back((static_cast<double>(i) - zero_index) / sample_rate_hz - t_mid);
        yc.push_back(y[i]);
    }

    double best_phase = 0, best = std::numeric_limits<double>::infinity();
    for (int k = 0; k < 360; k++)
    {
        const double p = k / 360.0;
        const double s = score(solveLevels(wave, frequency_hz, p, tc, yc));
        if (s < best)
        {
            best = s;
            best_phase = p;
        }
    }
    double f_fit = frequency_hz, phase_mid = best_phase;
    for (int i = -10; i <= 10; i++)
    {
        const double f = frequency_hz * (1 + frequency_span * i / 10.0);
        for (int j = -10; j <= 10; j++)
        {
            const double p = best_phase + j / 3600.0;
            const double s = score(solveLevels(wave, f, p, tc, yc));
            if (s < best)
            {
                best = s;
                f_fit = f;
                phase_mid = p;
            }
        }
    }
    if (!std::isfinite(best))
        throw std::runtime_error(std::string("no upright ") + waveName(wave) +
                                 " fits the samples");

    WaveFit fit;
    fit.frequency_hz = f_fit;
    const double phase0 = phase_mid - f_fit * t_mid;
    fit.phase = phase0 - std::floor(phase0);
    std::vector<double> t(n);
    for (size_t i = 0; i < n; i++)
        t[i] = (static_cast<double>(i) - zero_index) / sample_rate_hz;
    const Levels l = solveLevels(wave, f_fit, fit.phase, t, y);
    if (!(l.b > 0))
        throw std::runtime_error(std::string("no upright ") + waveName(wave) +
                                 " fits the samples");
    fit.low_v = l.a;
    fit.high_v = l.a + l.b;
    fit.residual_rms_v = std::sqrt(l.sse / static_cast<double>(n));
    fit.residual_fraction = fit.residual_rms_v / l.b;
    return fit;
}

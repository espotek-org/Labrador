#include "autoqa/HwTests.h"

#include "autoqa/Plot.h"
#include "autoqa/Tolerances.h"
#include "platform/paths.h"

#include "DeviceSession.h"
#include "JsonWriter.h"
#include "SampleCsv.h"
#include "Signal.h"
#include "SignalStats.h"
#include "StreamCheck.h"
#include "WaveFit.h"

#include "librador.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace autoqa
{
namespace
{
using Wave = SignalSpec::Wave;

constexpr double SETTLE_S = 0.5;          // after configuring the generators and scope
constexpr size_t CSV_KEEP_ROWS = 5000;    // rows saved per CSV artifact
// Windows a measurement takes before judging one that had lost or corrupt USB frames.
constexpr int MEASURE_TRIES = 3;
constexpr double SOAK_SECONDS = 60;
constexpr double THOROUGH_SOAK_SECONDS = 300; // a run at the thorough level
// The hardware sessions pass librador's debug messages to the log (the iso6 realignment
// among them, without which a misaligned stream on a test machine fails invisibly).
constexpr bool LIBRADOR_DEBUG_LOG = true;
constexpr double MAX_SHAPE_FREQUENCY_HZ = 20000; // above: under 19 samples a period
// The gain tests capture this long and judge each input on its longest stretch
// of undisturbed periods, which must be at least GAIN_MIN_STRETCH_S.
constexpr double GAIN_WINDOW_S = 0.25;
constexpr double GAIN_MIN_STRETCH_S = 0.04;
const Wave WAVES[] = {Wave::Sine, Wave::Square, Wave::Triangle, Wave::Sawtooth};
// The waveform tests run at 2237 Hz and again at exactly 831 Hz (owner's requirement, a hard
// constraint: every waveform test has an 831 Hz version). The scope streams 375 kS/s in 1 ms
// USB frames of 375 samples, and a frequency whose period divides that frame evenly (500 Hz,
// 1 kHz, 2 kHz) repeats exactly every frame, so a frame that librador back-fills with the
// previous one, or an endpoint a frame out of step, reads exactly like a clean capture. Both
// frequencies here are incommensurate with the frame: 2237 Hz is prime and 167.635 samples a
// period, and 831 Hz is 451.26 samples a period, so a dropped, repeated or shifted frame shows
// up as a kink, a step, a wrong pulse width or a wrong cycle count. Two are kept because they
// sit on either side of the frame length (several periods a frame against more than a frame a
// period) and the 831 Hz battery is a hard constraint of its own.
constexpr double BASE_HZ = 2237;
constexpr double ODD_HZ = 831.0;

// Whole periods [begin, end) of a sine capture whose residual from the window's
// fit stays near the typical period's, and how many periods did not.
struct Stretch
{
    size_t begin = 0;
    size_t end = 0;
    int disturbed = 0;
};

// Residual sum of squares of y against the best a*sin + b*cos + c at frequency f.
double sineSse(const std::vector<double>& y, double rate, double f)
{
    double ss = 0, cc = 0, sc = 0, s1 = 0, c1 = 0, ys = 0, yc = 0, y1 = 0, yy = 0;
    // sin and cos of w*i by rotating a phasor, renormalised now and then; calling sin and cos
    // per sample made a gain test take half a minute in a busy Windows VM.
    const double w = 2 * M_PI * f / rate;
    const double cw = std::cos(w), sw = std::sin(w);
    double s = 0, c = 1;
    for (size_t i = 0; i < y.size(); i++)
    {
        if (i % 4096 == 0)
        {
            s = std::sin(w * static_cast<double>(i));
            c = std::cos(w * static_cast<double>(i));
        }
        ss += s * s;
        cc += c * c;
        sc += s * c;
        s1 += s;
        c1 += c;
        ys += y[i] * s;
        yc += y[i] * c;
        y1 += y[i];
        yy += y[i] * y[i];
        const double next_s = s * cw + c * sw;
        c = c * cw - s * sw;
        s = next_s;
    }
    const double n = static_cast<double>(y.size());
    // Solve [ss sc s1; sc cc c1; s1 c1 n] [a b k] = [ys yc y1] by Cramer's rule.
    const double det = ss * (cc * n - c1 * c1) - sc * (sc * n - c1 * s1) + s1 * (sc * c1 - cc * s1);
    if (!(std::fabs(det) > 0))
        throw std::runtime_error("sineSse: singular fit (too few samples for the frequency)");
    const double a = (ys * (cc * n - c1 * c1) - sc * (yc * n - c1 * y1) + s1 * (yc * c1 - cc * y1)) / det;
    const double b = (ss * (yc * n - y1 * c1) - ys * (sc * n - c1 * s1) + s1 * (sc * y1 - yc * s1)) / det;
    const double k = (ss * (cc * y1 - c1 * yc) - sc * (sc * y1 - c1 * ys) + s1 * (sc * yc - cc * ys)) / det;
    return yy - (a * ys + b * yc + k * y1);
}

// The least-squares frequency of a sine near f0: a grid over +/-2% in 0.05% steps (the fit's
// main lobe is about 1/T wide, 0.45% for a 0.1 s stretch at 2237 Hz), then two finer grids
// around the best point, to 0.000125%.
double sineFrequency(const std::vector<double>& y, double rate, double f0)
{
    double best_f = f0;
    double span = 0.02, step = 0.0005;
    for (int pass = 0; pass < 3; pass++)
    {
        const double centre = best_f;
        double best = std::numeric_limits<double>::infinity();
        for (double r = -span; r <= span + step / 2; r += step)
        {
            const double f = centre * (1 + r);
            const double sse = sineSse(y, rate, f);
            if (sse < best)
            {
                best = sse;
                best_f = f;
            }
        }
        span = step * 2;
        step /= 20;
    }
    return best_f;
}

// The longest run of periods whose fit residual stays near the typical period's: a
// period is disturbed when its residual RMS, or its largest residual, is over three
// times the median period's plus one LSB (the LSB so that the near-perfect periods of a
// quiet input don't make ordinary noise look like a disturbance). The largest residual
// catches a lone spike, which barely moves a period's RMS: one such sample at the
// bottom of gain 64's range failed hw.gain.x64's "minimum inside the range".
Stretch steadyStretch(const std::vector<double>& y, double rate, double frequency_hz, double lsb)
{
    const WaveFit fit = fitWave(Wave::Sine, frequency_hz, tol::FREQUENCY, y, rate, 0);
    const double per_period = rate / fit.frequency_hz;
    const int periods = static_cast<int>(static_cast<double>(y.size()) / per_period);
    if (periods < 3)
        throw std::logic_error("steadyStretch: fewer than 3 periods in the window");
    std::vector<size_t> edge(periods + 1);
    for (int k = 0; k <= periods; k++)
        edge[k] = static_cast<size_t>(std::llround(k * per_period));
    std::vector<double> rms(periods), peak(periods);
    for (int k = 0; k < periods; k++)
    {
        double squares = 0, largest = 0;
        for (size_t i = edge[k]; i < edge[k + 1]; i++)
        {
            const double r = y[i] - fit.at(Wave::Sine, static_cast<double>(i) / rate);
            squares += r * r;
            largest = std::max(largest, std::fabs(r));
        }
        rms[k] = std::sqrt(squares / static_cast<double>(edge[k + 1] - edge[k]));
        peak[k] = largest;
    }
    const auto median = [periods](std::vector<double> v) {
        std::nth_element(v.begin(), v.begin() + periods / 2, v.end());
        return v[periods / 2];
    };
    const double rms_limit = 3 * median(rms) + lsb;
    const double peak_limit = 3 * median(peak) + lsb;
    Stretch best;
    int run_start = 0;
    for (int k = 0; k <= periods; k++)
    {
        if (k < periods && rms[k] <= rms_limit && peak[k] <= peak_limit)
            continue;
        if (k < periods)
            best.disturbed++;
        if (edge[k] - edge[run_start] > best.end - best.begin)
        {
            best.begin = edge[run_start];
            best.end = edge[k];
        }
        run_start = k + 1;
    }
    return best;
}
const char* TRANSPORTS[] = {"bulk", "iso1", "iso6"};

double shapeLimit(Wave w)
{
    switch (w)
    {
    case Wave::Sine: return tol::SHAPE_SINE;
    case Wave::Triangle: return tol::SHAPE_TRIANGLE;
    case Wave::Sawtooth: return tol::SHAPE_SAWTOOTH;
    case Wave::Square: return tol::SHAPE_SQUARE;
    default: throw std::logic_error("no shape limit");
    }
}

std::string num(double v)
{
    char text[32];
    std::snprintf(text, sizeof text, "%g", v);
    return text;
}

// A number for a test id: "0.5" becomes "0_5".
std::string idNum(double v)
{
    std::string s = num(v);
    std::replace(s.begin(), s.end(), '.', '_');
    return s;
}

// Lowest PSU setting that lets the generators reach both peaks.
double psuFor(const SignalSpec& a, const SignalSpec& b)
{
    const double peak = std::max(a.peakV(), b.peakV());
    return std::max(PSU_MIN_V, std::ceil((peak + SIGGEN_PSU_HEADROOM_V) * 2) / 2);
}

UsbTransport parseTransport(const std::string& name)
{
    if (name == "bulk")
        return UsbTransport::Bulk;
    if (name == "iso1")
        return UsbTransport::Iso1;
    if (name == "iso6")
        return UsbTransport::Iso6;
    throw std::logic_error("unknown transport " + name);
}

std::string version(unsigned v, unsigned variant)
{
    char text[40];
    std::snprintf(text, sizeof text, "0x%04x variant %u", v, variant);
    return text;
}

struct Capture
{
    SignalSpec spec[2];
    SignalOutput out[2];
    double psu_v = 0;
    double gain = 4;
    SampleWindow window;
    ChannelStats stats[2];

    const std::vector<double>& samples(int ch) const { return ch == 1 ? window.ch1 : window.ch2; }
};

class Suite;
using TestFn = std::function<void(Suite&, TestContext&)>;
struct HwTest
{
    TestSpec spec;
    TestFn fn;
};

// How long each transport soak streams at a level: thorough runs the long soak.
double soakSeconds(Level level)
{
    return level == Level::Thorough ? THOROUGH_SOAK_SECONDS : SOAK_SECONDS;
}

// One Suite serves a whole run (every iteration, both the hardware phase and the firmware
// round trip), so what hw.board.present learnt about the board reaches the round trip.
class Suite
{
  public:
    Suite(Runner& runner, std::string out_dir, Level level, std::function<void(const std::string&)> status)
        : m_runner(runner), m_out(std::move(out_dir)), m_status(std::move(status)), m_soak_s(soakSeconds(level)),
          m_tests(catalogue(level))
    {
    }

    static std::vector<HwTest> catalogue(Level level);
    static void declareConditions(Runner& runner);
    // Back to the state the first iteration started from: no device session (a transport
    // test leaves one on its own transport behind) and nothing known about the board.
    void startIteration();
    // Runs these planned tests in order, on the calling thread.
    void run(const std::vector<PlannedTest>& planned);

  private:
    // --- helpers ---------------------------------------------------------
    DeviceSession& session()
    {
        if (!m_session)
            m_session = std::make_unique<DeviceSession>(LIBRADOR_DEBUG_LOG, UsbTransport::Auto);
        return *m_session;
    }

    Capture measure(TestContext& ctx, const std::string& name, const SignalSpec& sg1,
                    const SignalSpec& sg2, double gain = 4, ScopeMode mode = ScopeMode::Ch1Ch2,
                    double seconds = 0.05)
    {
        Capture c;
        c.spec[0] = sg1;
        c.spec[1] = sg2;
        c.gain = gain;
        c.psu_v = psuFor(sg1, sg2);
        DeviceSession& s = session();
        applyPsu(c.psu_v);
        c.out[0] = applySignal(1, sg1);
        c.out[1] = applySignal(2, sg2);
        s.configureScope(mode, gain, SETTLE_S);
        // A window with lost or corrupt USB frames is captured again, up to MEASURE_TRIES times:
        // these tests judge the analog path, and the hw.transport tests judge the stream. A
        // stream fault that spoils every window still fails here (the last window is judged).
        int spoilt = 0;
        for (int attempt = 1;; attempt++)
        {
            c.window = s.acquire(seconds);
            const FrameStats& f = c.window.frames;
            if ((f.dropped == 0 && f.bad_checksum == 0) || attempt == MEASURE_TRIES)
                break;
            spoilt++;
        }
        if (spoilt)
            ctx.note(name + ": " + std::to_string(spoilt) + " earlier window" + (spoilt == 1 ? " was" : "s were") +
                     " captured again for lost or corrupt USB frames");
        const double lsb = scopeLsbVolts(gain);
        c.stats[0] = analyseChannel(c.window.ch1, c.window.sample_rate_hz, lsb);
        if (!c.window.ch2.empty())
            c.stats[1] = analyseChannel(c.window.ch2, c.window.sample_rate_hz, lsb);

        const std::string csv = ctx.path("captures", name + ".csv");
        {
            const size_t n = std::min(CSV_KEEP_ROWS, c.window.ch1.size());
            const std::vector<double> ch1(c.window.ch1.begin(), c.window.ch1.begin() + n);
            const std::vector<double> ch2 = c.window.ch2.empty()
                ? std::vector<double>()
                : std::vector<double>(c.window.ch2.begin(), c.window.ch2.begin() + n);
            std::ofstream out(ctx.absolute(csv), std::ios::binary | std::ios::trunc);
            writeSamplesCsv(out, ch1, ch2, c.window.sample_rate_hz, 0);
        }
        ctx.artifact(csv, "csv", name + ": first " + std::to_string(CSV_KEEP_ROWS) + " samples (SG1 " +
                                     sg1.text() + ", SG2 " + sg2.text() + ", gain " + num(gain) + ")");
        const FrameStats& f = c.window.frames;
        if (f.dropped || f.bad_checksum)
            ctx.note(name + ": " + std::to_string(f.dropped + f.bad_checksum) + " of " +
                     std::to_string(f.ok + f.dropped + f.bad_checksum) +
                     " USB frames were lost or corrupt during the window (filled with repeats); "
                     "the hw.transport tests measure this");
        return c;
    }

    void checkDacLevels(TestContext& ctx, const std::string& label, const SignalSpec& spec,
                        const SignalOutput& out)
    {
        const double step = tol::dacStepV(spec.peakV());
        ctx.measure(label + " DAC low level", "V", out.v_min, spec.base_v,
                    spec.base_v - 1.5 * step, spec.base_v + 1.5 * step);
        if (spec.isPeriodic())
            ctx.measure(label + " DAC high level", "V", out.v_max, spec.peakV(),
                        spec.peakV() - 1.5 * step, spec.peakV() + 1.5 * step);
    }

    void checkFrequency(TestContext& ctx, const std::string& label, const SignalSpec& spec,
                        const SignalOutput& out, const ChannelStats& stats)
    {
        const double req = spec.frequency_hz, real = out.frequency_hz;
        ctx.measure(label + " realised frequency", "Hz", real, req, req * (1 - tol::REALISED_FREQUENCY),
                    req * (1 + tol::REALISED_FREQUENCY));
        ctx.measure(label + " frequency", "Hz", stats.periodic ? stats.frequency_hz : NAN, real,
                    real * (1 - tol::FREQUENCY), real * (1 + tol::FREQUENCY));
    }

    // Frequency, fitted levels and (unless check_shape is false) shape of
    // generator `ch` as seen on input `ch`, plus a plot.
    void checkWave(TestContext& ctx, const std::string& label, const Capture& c, int ch,
                   const std::string& title, bool check_shape = true)
    {
        const SignalSpec& spec = c.spec[ch - 1];
        const SignalOutput& out = c.out[ch - 1];
        checkFrequency(ctx, label, spec, out, c.stats[ch - 1]);
        const std::vector<double>& y = c.samples(ch);
        const double rate = c.window.sample_rate_hz;
        const WaveFit fit = fitWave(spec.wave, out.frequency_hz, tol::FREQUENCY, y, rate, 0);
        const double lo_tol = tol::level(out.v_min, c.gain), hi_tol = tol::level(out.v_max, c.gain);
        ctx.measure(label + " low level", "V", fit.low_v, out.v_min, out.v_min - lo_tol, out.v_min + lo_tol);
        ctx.measure(label + " high level", "V", fit.high_v, out.v_max, out.v_max - hi_tol, out.v_max + hi_tol);
        if (check_shape)
        {
            ctx.measure(label + " shape residual", "fraction", fit.residual_fraction, std::nullopt,
                        std::nullopt, shapeLimit(spec.wave));
            double nearest = INFINITY;
            const char* nearest_name = "";
            for (const Wave w : WAVES)
            {
                if (w == spec.wave)
                    continue;
                const double r = fitWave(w, out.frequency_hz, tol::FREQUENCY, y, rate, 0).residual_fraction;
                if (r < nearest)
                {
                    nearest = r;
                    nearest_name = waveName(w);
                }
            }
            ctx.measure(label + " next-best shape (" + nearest_name + ") residual / " +
                            waveName(spec.wave) + " residual",
                        "ratio", nearest / fit.residual_fraction, std::nullopt, 1.0);
        }
        else
            ctx.measure(label + " shape residual (not judged: too few samples per period)", "fraction",
                        fit.residual_fraction);
        if (spec.wave == Wave::Square)
            ctx.measure(label + " duty cycle", "fraction",
                        c.stats[ch - 1].periodic ? c.stats[ch - 1].duty : NAN, 0.5,
                        0.5 - tol::SQUARE_DUTY, 0.5 + tol::SQUARE_DUTY);
        plotWave(ctx, label, c, ch, fit, title);
    }

    void plotWave(TestContext& ctx, const std::string& label, const Capture& c, int ch,
                  const WaveFit& fit, const std::string& title)
    {
        const SignalSpec& spec = c.spec[ch - 1];
        const SignalOutput& out = c.out[ch - 1];
        const double rate = c.window.sample_rate_hz;
        const double span = 5.0 / out.frequency_hz;
        const size_t n = std::min(c.samples(ch).size(), static_cast<size_t>(span * rate) + 1);
        const bool ms = span > 2e-3;
        const double scale = ms ? 1e3 : 1e6;
        Plot::Series measured, other, fitted, expected;
        measured.label = "CH" + std::to_string(ch) + " measured";
        const int other_ch = ch == 1 ? 2 : 1;
        other.label = c.samples(other_ch).empty() ? "" : "CH" + std::to_string(other_ch);
        other.rgb = Plot::GREY;
        other.width = 1;
        fitted.label = "best fit of the expected shape";
        fitted.rgb = Plot::RED;
        fitted.width = 1;
        char exp_label[96];
        std::snprintf(exp_label, sizeof exp_label, "expected: %s %.3f-%.3f V", waveName(spec.wave),
                      out.v_min, out.v_max);
        expected.label = exp_label;
        expected.rgb = Plot::GREEN;
        expected.dashed = true;
        for (size_t i = 0; i < n; i++)
        {
            const double t = i / rate;
            measured.x.push_back(t * scale);
            measured.y.push_back(c.samples(ch)[i]);
            if (!c.samples(other_ch).empty())
            {
                other.x.push_back(t * scale);
                other.y.push_back(c.samples(other_ch)[i]);
            }
            fitted.x.push_back(t * scale);
            fitted.y.push_back(fit.at(spec.wave, t));
            expected.x.push_back(t * scale);
            expected.y.push_back(out.v_min + (out.v_max - out.v_min) *
                                                 idealShape(spec.wave, fit.frequency_hz * t + fit.phase));
        }
        Plot plot(title, ms ? "time (ms)" : "time (us)", "volts");
        if (!other.x.empty())
            plot.add(other);
        plot.add(measured);
        plot.add(fitted);
        plot.add(expected);
        std::string file = label;
        std::replace(file.begin(), file.end(), ' ', '_');
        const std::string png = ctx.path("plots", file + ".png");
        plot.save(ctx.absolute(png));
        ctx.artifact(png, "png", label + ": captured waveform (blue), best fit (red) and expected (green dashed)");
    }

    // --- tests -------------------------------------------------------------
    void boardPresent(TestContext& ctx);
    void wiring(TestContext& ctx, int ch);
    void noise(TestContext& ctx, int ch);
    void wave(TestContext& ctx, int ch, Wave w, double hz);
    void bigSine(TestContext& ctx, int ch, double hz);
    void dcLevels(TestContext& ctx);
    void gain(TestContext& ctx, double g, double hz);
    void sweep(TestContext& ctx, double f);
    void modeScope1(TestContext& ctx, double hz);
    void mode750k(TestContext& ctx, double hz);
    void isolation(TestContext& ctx, int src, Wave w);
    void trigger(TestContext& ctx, int ch, bool rising, double level, Wave w, double hz);
    void triggerPlacement(TestContext& ctx, double hz);
    void triggerTimeout(TestContext& ctx);
    void triggerDc(TestContext& ctx);
    void transportMeasure(TestContext& ctx, const std::string& t, double hz, bool gates_soak);
    void transportSoak(TestContext& ctx, const std::string& t);
    void firmwareRoundTrip(TestContext& ctx);
    void ensureBundledFirmware(TestContext& ctx);

    Runner& m_runner;
    std::string m_out;
    std::function<void(const std::string&)> m_status;
    double m_soak_s;
    std::vector<HwTest> m_tests;
    std::unique_ptr<DeviceSession> m_session;
    bool m_calibration_stored = false;
};

// ---------------------------------------------------------------------------
void Suite::boardPresent(TestContext& ctx)
{
    try
    {
        session();
    }
    catch (const std::runtime_error& e)
    {
        ctx.fail(std::string("no usable board: ") + e.what() +
                 " (the hardware tests need a board with SG1 wired to CH1 and SG2 to CH2)");
    }
    const DeviceSession& s = *m_session;
    ctx.measureText("firmware", version(librador_get_device_firmware_version(),
                                        librador_get_device_firmware_variant()),
                    version(EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT));
    ctx.measureText("default USB transport", transportName(s.activeTransport()));
    m_calibration_stored = s.calibration().stored;
    ctx.measureText("calibration stored on the board", m_calibration_stored ? "yes" : "no");
    m_runner.setCondition("board", true);
}

void Suite::wiring(TestContext& ctx, int ch)
{
    const int other = ch == 1 ? 2 : 1;
    const SignalSpec sine = SignalSpec::periodic(Wave::Sine, BASE_HZ, 2, 0.5);
    const Capture c = ch == 1 ? measure(ctx, "sg1_only", sine, SignalSpec::off())
                              : measure(ctx, "sg2_only", SignalSpec::off(), sine);
    const ChannelStats& driven = c.stats[ch - 1];
    const ChannelStats& idle = c.stats[other - 1];
    ctx.measure("CH" + std::to_string(ch) + " swing", "Vpp", driven.vpp, 2.0, 1.0);
    ctx.measure("CH" + std::to_string(other) + " AC RMS (should stay at the noise floor)", "V",
                idle.rms_ac_v, std::nullopt, std::nullopt, 0.1);
    const std::string cond = "sg" + std::to_string(ch) + "_ch" + std::to_string(ch);
    if (driven.vpp < 1.0)
    {
        char reason[200];
        if (idle.vpp >= 1.0)
            std::snprintf(reason, sizeof reason,
                          "SG%d's sine appears on CH%d instead of CH%d: the loopback wires look swapped",
                          ch, other, ch);
        else
            std::snprintf(reason, sizeof reason,
                          "CH%d reads the noise floor (Vpp %.3f V) while SG%d drives 2 Vpp: the "
                          "SG%d->CH%d loopback is not wired",
                          ch, driven.vpp, ch, ch, ch);
        m_runner.setCondition(cond, false, std::string("needs the SG->CH loopback: ") + reason);
        ctx.fail(reason);
    }
    if (idle.rms_ac_v > 0.1)
    {
        const std::string reason = "SG" + std::to_string(ch) + " also appears on CH" +
                                   std::to_string(other) + ": the inputs look shorted together";
        m_runner.setCondition(cond, false, reason);
        ctx.fail(reason);
    }
    m_runner.setCondition(cond, true);
    ctx.note("measured wiring: SG" + std::to_string(ch) + " -> CH" + std::to_string(ch));
}

void Suite::noise(TestContext& ctx, int ch)
{
    const Capture c = measure(ctx, "parked", SignalSpec::off(), SignalSpec::off(), 4,
                              ScopeMode::Ch1Ch2, 0.1);
    const ChannelStats& s = c.stats[ch - 1];
    const double parked = c.out[ch - 1].v_min;
    ctx.measure("CH" + std::to_string(ch) + " AC RMS", "V", s.rms_ac_v, std::nullopt, std::nullopt,
                tol::NOISE_RMS_V);
    ctx.measure("CH" + std::to_string(ch) + " mean (generator parked)", "V", s.mean_v, parked,
                parked - tol::level(parked, 4), parked + tol::level(parked, 4));
}

void Suite::wave(TestContext& ctx, int ch, Wave w, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(w, hz, 2, 0.5);
    const std::string name = std::string("sg") + std::to_string(ch) + "_" + waveName(w);
    const Capture c = ch == 1 ? measure(ctx, name, spec, SignalSpec::off())
                              : measure(ctx, name, SignalSpec::off(), spec);
    checkDacLevels(ctx, "SG" + std::to_string(ch), spec, c.out[ch - 1]);
    checkWave(ctx, "CH" + std::to_string(ch), c, ch,
              "SG" + std::to_string(ch) + " " + spec.text() + " -> CH" + std::to_string(ch) + ", gain 4");
}

void Suite::bigSine(TestContext& ctx, int ch, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(Wave::Sine, hz, 6, 0.5);
    const Capture c = ch == 1 ? measure(ctx, "big", spec, SignalSpec::off(), 2)
                              : measure(ctx, "big", SignalSpec::off(), spec, 2);
    ctx.measure("PSU", "V", c.psu_v, 8.0);
    checkDacLevels(ctx, "SG" + std::to_string(ch), spec, c.out[ch - 1]);
    checkWave(ctx, "CH" + std::to_string(ch), c, ch,
              "SG" + std::to_string(ch) + " " + spec.text() + " (x3 stage, PSU 8 V) -> CH" +
                  std::to_string(ch) + ", gain 2");
}

void Suite::dcLevels(TestContext& ctx)
{
    std::vector<double> set[2], err[2], tol_hi[2];
    for (const double level : {0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 6.0})
    {
        const SignalSpec spec = SignalSpec::dc(level);
        const Capture c = measure(ctx, "dc_" + num(level), spec, spec);
        for (int ch = 1; ch <= 2; ch++)
        {
            const double actual = c.out[ch - 1].v_min;
            const double t = tol::level(actual, 4);
            const double mean = c.stats[ch - 1].mean_v;
            ctx.measure("CH" + std::to_string(ch) + " at " + num(level) + " V", "V", mean, actual,
                        actual - t, actual + t);
            set[ch - 1].push_back(actual);
            err[ch - 1].push_back(mean - actual);
            tol_hi[ch - 1].push_back(t);
        }
    }
    for (int ch = 1; ch <= 2; ch++)
    {
        // Least-squares line through (set, measured).
        const std::vector<double>& x = set[ch - 1];
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < x.size(); i++)
        {
            const double y = x[i] + err[ch - 1][i];
            sx += x[i];
            sy += y;
            sxx += x[i] * x[i];
            sxy += x[i] * y;
        }
        const double n = static_cast<double>(x.size());
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        const double intercept = (sy - slope * sx) / n;
        ctx.measure("CH" + std::to_string(ch) + " gain (slope of measured vs set)", "ratio", slope, 1.0,
                    1 - tol::CHANNEL_GAIN, 1 + tol::CHANNEL_GAIN);
        ctx.measure("CH" + std::to_string(ch) + " offset (intercept)", "V", intercept, 0.0);

        Plot plot("SG" + std::to_string(ch) + " DC levels read on CH" + std::to_string(ch) +
                      " (gain 4): error against the limit",
                  "set level (V)", "measured - set (V)");
        Plot::Series e, lim_hi, lim_lo;
        e.x = x;
        e.y = err[ch - 1];
        e.markers = true;
        e.label = "CH" + std::to_string(ch) + " error";
        lim_hi.x = x;
        lim_lo.x = x;
        for (const double t : tol_hi[ch - 1])
        {
            lim_hi.y.push_back(t);
            lim_lo.y.push_back(-t);
        }
        lim_hi.rgb = lim_lo.rgb = Plot::GREEN;
        lim_hi.dashed = lim_lo.dashed = true;
        lim_hi.label = "limit";
        plot.add(lim_hi);
        plot.add(lim_lo);
        plot.add(e);
        plot.hline(0, Plot::GREY, "");
        const std::string png = ctx.path("plots", "ch" + std::to_string(ch) + "_dc.png");
        plot.save(ctx.absolute(png));
        ctx.artifact(png, "png", "CH" + std::to_string(ch) + ": DC reading error against its limit");
    }
}

// One sine per scope gain (at 2237 Hz, and at 831 Hz for the _831hz tests), filling a good
// part of that gain's input range (1.65 V +/- 23.65 V / gain) without clipping. The generators cannot go below
// about 0.06 V or above 9.6 V, so the low gains' ranges are only partly used.
SignalSpec gainSignal(double g, double hz)
{
    if (g == 64) return SignalSpec::periodic(Wave::Sine, hz, 0.4, 1.45);  // range 1.28-2.02 V
    if (g == 32) return SignalSpec::periodic(Wave::Sine, hz, 0.9, 1.2);   // 0.91-2.39 V
    if (g == 16) return SignalSpec::periodic(Wave::Sine, hz, 1.8, 0.75);  // 0.17-3.13 V
    if (g == 8) return SignalSpec::periodic(Wave::Sine, hz, 2.8, 0.3);    // -1.31-4.61 V
    if (g == 4) return SignalSpec::periodic(Wave::Sine, hz, 5, 0.5);      // -4.26-7.56 V
    if (g == 2) return SignalSpec::periodic(Wave::Sine, hz, 6, 0.5);      // -10.2-13.5 V
    if (g == 1 || g == 0.5) return SignalSpec::periodic(Wave::Sine, hz, 8, 0.5);
    throw std::logic_error("no signal for gain " + num(g));
}

void Suite::gain(TestContext& ctx, double g, double hz)
{
    const SignalSpec spec = gainSignal(g, hz);
    const Capture whole = measure(ctx, "gain_" + num(g), spec, spec, g, ScopeMode::Ch1Ch2, GAIN_WINDOW_S);
    const double lsb = tol::lsbV(g);
    const double rate = whole.window.sample_rate_hz;
    for (int ch = 1; ch <= 2; ch++)
    {
        // At the high gains a disturbance from outside the board swamps the small
        // signal for a few milliseconds (on the QA tester, a burst on CH2 every
        // 102.4 ms, the Wi-Fi beacon interval, from the router next to it). Each
        // input is judged on its longest undisturbed stretch, and what was left
        // out is recorded.
        const std::string name = "CH" + std::to_string(ch);
        const Stretch st = steadyStretch(whole.samples(ch), rate, whole.out[ch - 1].frequency_hz, scopeLsbVolts(g));
        ctx.measure(name + " periods disturbed (fit residual RMS or peak over 3x the median period's), left out",
                    "periods",
                    st.disturbed);
        ctx.measure(name + " undisturbed stretch judged", "s", static_cast<double>(st.end - st.begin) / rate,
                    std::nullopt, GAIN_MIN_STRETCH_S);
        if (st.disturbed)
            ctx.note(name + ": " + std::to_string(st.disturbed) + " disturbed periods left out of a " +
                     num(GAIN_WINDOW_S) + " s window");
        Capture c = whole;
        c.window.ch1.assign(whole.window.ch1.begin() + st.begin, whole.window.ch1.begin() + st.end);
        c.window.ch2.assign(whole.window.ch2.begin() + st.begin, whole.window.ch2.begin() + st.end);
        c.stats[0] = analyseChannel(c.window.ch1, rate, scopeLsbVolts(g));
        c.stats[1] = analyseChannel(c.window.ch2, rate, scopeLsbVolts(g));
        // The frequency from a least-squares sine fit over the stretch, not from counting
        // level crossings: at gain 64 the noise between disturbances reaches the crossing
        // detector's hysteresis (1/8 of the range) and adds crossings of its own.
        c.stats[ch - 1].frequency_hz = sineFrequency(c.samples(ch), rate, whole.out[ch - 1].frequency_hz);
        // Clipping is judged on the 0.1st and 99.9th percentiles, not the single lowest and
        // highest samples: a clipped sine sits at the range's end for a run of samples every
        // period, while at gain 64 one noise sample at a trough (86 mV above the bottom code,
        // with CH2's noise peaking near 50 mV on the QA tester) can touch it on its own.
        std::vector<double> sorted(c.samples(ch).begin(), c.samples(ch).end());
        std::sort(sorted.begin(), sorted.end());
        const size_t tail = sorted.size() / 1000;
        ctx.measure("CH" + std::to_string(ch) + " low end (0.1st percentile) inside the range", "V", sorted[tail],
                    std::nullopt, tol::scopeLow(g) + lsb / 2);
        ctx.measure("CH" + std::to_string(ch) + " high end (99.9th percentile) inside the range", "V",
                    sorted[sorted.size() - 1 - tail], std::nullopt, std::nullopt, tol::scopeHigh(g) - lsb / 2);
        checkWave(ctx, "CH" + std::to_string(ch), c, ch,
                  "Gain x" + num(g) + ": " + spec.text() + " on SG1 and SG2, CH" + std::to_string(ch));
    }
}

void Suite::sweep(TestContext& ctx, double f)
{
    const SignalSpec spec = SignalSpec::periodic(Wave::Sine, f, 2, 0.5);
    const double seconds = std::max(0.05, 20 / f);
    // One generator at a time: with both running, the DAC steps of one couple
    // into the other input and spoil its shape above about 10 kHz.
    for (int ch = 1; ch <= 2; ch++)
    {
        const std::string name = "sweep_sg" + std::to_string(ch);
        const Capture c = ch == 1 ? measure(ctx, name, spec, SignalSpec::off(), 4, ScopeMode::Ch1Ch2, seconds)
                                  : measure(ctx, name, SignalSpec::off(), spec, 4, ScopeMode::Ch1Ch2, seconds);
        checkWave(ctx, "CH" + std::to_string(ch), c, ch,
                  num(f) + " Hz sine on SG" + std::to_string(ch) + ", CH" + std::to_string(ch),
                  f <= MAX_SHAPE_FREQUENCY_HZ);
    }
}

void Suite::modeScope1(TestContext& ctx, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(Wave::Sine, hz, 2, 0.5);
    const Capture c = measure(ctx, "scope1", spec, SignalSpec::off(), 4, ScopeMode::Ch1);
    ctx.measure("sample rate", "S/s", c.window.sample_rate_hz, 375000, 375000, 375000);
    ctx.measureText("CH2 streamed", c.window.ch2.empty() ? "no" : "yes", "no");
    checkWave(ctx, "CH1", c, 1, "scope1: SG1 " + spec.text() + " -> CH1");
}

void Suite::mode750k(TestContext& ctx, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(Wave::Sine, hz, 2, 0.5);
    const Capture c = measure(ctx, "scope1_750k", spec, SignalSpec::off(), 4, ScopeMode::Ch1Fast);
    ctx.measure("sample rate", "S/s", c.window.sample_rate_hz, 750000, 750000, 750000);
    ctx.measureText("CH2 streamed", c.window.ch2.empty() ? "no" : "yes", "no");
    checkWave(ctx, "CH1", c, 1, "scope1-750k: SG1 " + spec.text() + " -> CH1");
}

void Suite::isolation(TestContext& ctx, int src, Wave w)
{
    const int dst = src == 1 ? 2 : 1;
    const SignalSpec spec = SignalSpec::periodic(w, BASE_HZ, 6, 0.5);
    const Capture c = src == 1 ? measure(ctx, "driven", spec, SignalSpec::off())
                               : measure(ctx, "driven", SignalSpec::off(), spec);
    const double driven = c.stats[src - 1].rms_ac_v, idle = c.stats[dst - 1].rms_ac_v;
    ctx.measure("CH" + std::to_string(src) + " (driven) AC RMS", "V", driven);
    ctx.measure("CH" + std::to_string(dst) + " (idle) AC RMS / CH" + std::to_string(src) + " AC RMS", "ratio",
                idle / driven, std::nullopt, std::nullopt,
                w == Wave::Square ? tol::CROSSTALK_SQUARE : tol::CROSSTALK_SINE);
    Plot plot("SG" + std::to_string(src) + " " + spec.text() + ", SG" + std::to_string(dst) +
                  " parked: crosstalk into CH" + std::to_string(dst),
              "time (ms)", "volts");
    Plot::Series a, b;
    const size_t n = std::min<size_t>(c.samples(src).size(), 940);
    for (size_t i = 0; i < n; i++)
    {
        a.x.push_back(i / c.window.sample_rate_hz * 1e3);
        a.y.push_back(c.samples(src)[i]);
        b.x.push_back(a.x.back());
        b.y.push_back(c.samples(dst)[i]);
    }
    a.rgb = Plot::GREY;
    a.width = 1;
    a.label = "CH" + std::to_string(src) + " (driven)";
    b.label = "CH" + std::to_string(dst) + " (idle)";
    plot.add(a);
    plot.add(b);
    const std::string png = ctx.path("plots", "isolation.png");
    plot.save(ctx.absolute(png));
    ctx.artifact(png, "png", "CH" + std::to_string(dst) + " should stay near flat while CH" +
                                 std::to_string(src) + " swings 6 V");
}

void Suite::trigger(TestContext& ctx, int ch, bool rising, double level, Wave w, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(w, hz, 2, 0.5);
    DeviceSession& s = session();
    applyPsu(PSU_MIN_V);
    SignalOutput out[2] = {applySignal(1, ch == 1 ? spec : SignalSpec::off()),
                           applySignal(2, ch == 2 ? spec : SignalSpec::off())};
    s.configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
    librador_capture_request req;
    req.trigger_channel = ch;
    req.rising = rising;
    req.level_v = level;
    req.hysteresis_v = 0.25;
    req.pre_s = 0.002;
    req.post_s = 0.004;
    req.timeout_s = 10;
    librador_capture_result res;
    const int rc = librador_capture_around_trigger(&req, &res);
    if (rc != 0)
        ctx.fail("librador_capture_around_trigger returned " + std::to_string(rc));
    const double rate = res.sample_rate_hz;
    const int expected_n = static_cast<int>(std::lround((req.pre_s + req.post_s) * rate));
    const int expected_pre = static_cast<int>(std::lround(req.pre_s * rate));
    ctx.measure("samples", "", static_cast<double>(res.ch1.size()), expected_n, expected_n, expected_n);
    ctx.measure("pre-trigger samples", "", res.pre_samples, expected_pre, expected_pre, expected_pre);
    const std::vector<double>& y = ch == 1 ? res.ch1 : res.ch2;
    const double at = y[res.pre_samples], before = y[res.pre_samples - 1];
    if (rising)
    {
        ctx.measure("trigger sample", "V", at, std::nullopt, level);
        ctx.measure("sample before the trigger", "V", before, std::nullopt, std::nullopt, level - 1e-9);
    }
    else
    {
        ctx.measure("trigger sample", "V", at, std::nullopt, std::nullopt, level);
        ctx.measure("sample before the trigger", "V", before, std::nullopt, level + 1e-9);
    }
    // The fitted generator waveform must cross the level, in the right
    // direction, at t = 0: the trigger fired on the signal, not on a glitch.
    const SignalOutput& o = out[ch - 1];
    const WaveFit fit = fitWave(w, o.frequency_hz, tol::FREQUENCY, y, rate, res.pre_samples);
    const double period = 1 / o.frequency_hz;
    double nearest = NAN;
    double prev = fit.at(w, -0.1 * period);
    for (int i = 1; i <= 2000; i++)
    {
        const double t = -0.1 * period + 0.2 * period * i / 2000;
        const double v = fit.at(w, t);
        const bool crossed = rising ? (v - level >= 0 && prev - level < 0) : (v - level <= 0 && prev - level > 0);
        if (crossed && (std::isnan(nearest) || std::fabs(t) < std::fabs(nearest)))
            nearest = t;
        prev = v;
    }
    const char* edge = rising ? "rising" : "falling";
    ctx.measure(std::string(edge) + " level crossing of the fitted " + waveName(w) + " nearest t=0", "s",
                nearest, 0.0, -3 / rate, 3 / rate);
    ctx.measure(std::string(waveName(w)) + " fit residual", "fraction", fit.residual_fraction, std::nullopt,
                std::nullopt, shapeLimit(w));
    ctx.measure("frames dropped while waiting", "", static_cast<double>(res.frames_dropped));

    Plot plot("capture trigger ch" + std::to_string(ch) + ":" + edge + ":" + num(level) + " on SG" +
                  std::to_string(ch) + " " + spec.text(),
              "time (us)", "volts");
    Plot::Series m, f;
    m.label = "CH" + std::to_string(ch);
    f.label = "best fit";
    f.rgb = Plot::RED;
    f.width = 1;
    const size_t end = std::min(y.size(), static_cast<size_t>(res.pre_samples + 1.5 * period * rate));
    for (size_t i = 0; i < end; i++)
    {
        const double t = (static_cast<double>(i) - res.pre_samples) / rate;
        m.x.push_back(t * 1e6);
        m.y.push_back(y[i]);
        f.x.push_back(t * 1e6);
        f.y.push_back(fit.at(w, t));
    }
    plot.add(m);
    plot.add(f);
    plot.vline(0, Plot::ORANGE, "trigger");
    plot.hline(level, Plot::ORANGE, "level " + num(level) + " V");
    const std::string png = ctx.path("plots", "trigger.png");
    plot.save(ctx.absolute(png));
    ctx.artifact(png, "png", std::string("CH") + std::to_string(ch) + " around the trigger: the " + edge +
                                 " edge should cross the dotted level at t=0");
}

void Suite::triggerPlacement(TestContext& ctx, double hz)
{
    const SignalSpec spec = SignalSpec::periodic(Wave::Sine, hz, 2, 0.5);
    DeviceSession& s = session();
    applyPsu(PSU_MIN_V);
    applySignal(1, spec);
    applySignal(2, SignalSpec::off());
    s.configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
    struct Case
    {
        const char* name;
        double pre, post;
    };
    for (const Case cs : {Case{"all_post", 0.0, 0.005}, Case{"mostly_pre", 0.009, 0.001}, Case{"long", 0.5, 1.0}})
    {
        librador_capture_request req;
        req.trigger_channel = 1;
        req.level_v = 1.5;
        req.pre_s = cs.pre;
        req.post_s = cs.post;
        req.timeout_s = 10;
        librador_capture_result res;
        const int rc = librador_capture_around_trigger(&req, &res);
        if (rc != 0)
            ctx.fail(std::string(cs.name) + ": librador_capture_around_trigger returned " + std::to_string(rc));
        const int n = static_cast<int>(std::lround((cs.pre + cs.post) * res.sample_rate_hz));
        const int pre = static_cast<int>(std::lround(cs.pre * res.sample_rate_hz));
        ctx.measure(std::string(cs.name) + ": samples", "", static_cast<double>(res.ch1.size()), n, n, n);
        ctx.measure(std::string(cs.name) + ": pre-trigger samples", "", res.pre_samples, pre, pre, pre);
        ctx.measure(std::string(cs.name) + ": trigger sample", "V", res.ch1[res.pre_samples], std::nullopt, 1.5);
        if (res.pre_samples > 0)
            ctx.measure(std::string(cs.name) + ": sample before the trigger", "V", res.ch1[res.pre_samples - 1],
                        std::nullopt, std::nullopt, 1.5 - 1e-9);
    }
}

void Suite::triggerTimeout(TestContext& ctx)
{
    DeviceSession& s = session();
    applyPsu(PSU_MIN_V);
    applySignal(1, SignalSpec::periodic(Wave::Sine, BASE_HZ, 2, 0.5));
    applySignal(2, SignalSpec::off());
    s.configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
    librador_capture_request req;
    req.trigger_channel = 1;
    req.level_v = 4.0; // above the 2.5 V peak
    req.timeout_s = 2;
    librador_capture_result res;
    const double t0 = nowSeconds();
    const int rc = librador_capture_around_trigger(&req, &res);
    ctx.measure("librador_capture_around_trigger result (-5: timed out)", "", rc, -5, -5, -5);
    ctx.measure("time to give up", "s", nowSeconds() - t0, 2.0, 2.0, 10.0);
}

void Suite::triggerDc(TestContext& ctx)
{
    DeviceSession& s = session();
    applyPsu(PSU_MIN_V);
    applySignal(1, SignalSpec::dc(1.5));
    applySignal(2, SignalSpec::off());
    s.configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
    librador_capture_request req;
    req.trigger_channel = 1;
    req.level_v = 1.5;
    req.hysteresis_v = 0.25;
    req.timeout_s = 2;
    librador_capture_result res;
    const int rc = librador_capture_around_trigger(&req, &res);
    ctx.measure("librador_capture_around_trigger result (-5: no trigger)", "", rc, -5, -5, -5);
}

// The 2237 Hz measure decides whether the transport is usable (gates_soak): its result is the
// condition the soak and the 831 Hz measure need. At 2237 Hz the triangle runs at twice the
// sine's frequency (4474 Hz), which tells the inputs apart; at 831 Hz both run at exactly
// 831 Hz and the wave shapes tell them apart.
void Suite::transportMeasure(TestContext& ctx, const std::string& t, double hz, bool gates_soak)
{
    m_session.reset();
    m_session = std::make_unique<DeviceSession>(LIBRADOR_DEBUG_LOG, parseTransport(t));
    const std::string active = transportName(m_session->activeTransport());
    if (active != t)
    {
#ifdef __APPLE__
        const std::string reason = "librador runs only bulk on macOS (iso transports panic macOS 26's USB "
                                   "stack); asked for " + t + ", got " + active;
        if (gates_soak)
            m_runner.setCondition("transport_" + t, false, reason);
        ctx.skip(reason);
#else
        const std::string reason = "asked for transport " + t + " but librador used " + active;
        if (gates_soak)
            m_runner.setCondition("transport_" + t, false, reason);
        ctx.fail(reason);
#endif
    }
    ctx.measureText("transport", active, t);
    // Both inputs at once, with smooth waves: a square's edges couple into the
    // other input as single-sample spikes, and one landing on the sine's
    // mid-level crossing adds a false crossing (seen when this test ran at 2 kHz, which read as
    // 2990 Hz).
    const SignalSpec a = SignalSpec::periodic(Wave::Sine, hz, 2, 0.5);
    const SignalSpec b = SignalSpec::periodic(Wave::Triangle, hz == BASE_HZ ? 2 * hz : hz, 1.5, 0.8);
    const Capture c = measure(ctx, t, a, b);
    checkWave(ctx, "CH1", c, 1, t + ": SG1 " + a.text() + " -> CH1");
    checkWave(ctx, "CH2", c, 2, t + ": SG2 " + b.text() + " -> CH2");
    if (gates_soak)
        m_runner.setCondition("transport_" + t, true);
}

void Suite::transportSoak(TestContext& ctx, const std::string& t)
{
    m_session.reset();
    m_session = std::make_unique<DeviceSession>(LIBRADOR_DEBUG_LOG, parseTransport(t));
    // 354.1 Hz and 1234.5 Hz have periods that are not a whole fraction of the
    // 375-sample USB frame, so a lost or repeated frame shifts the waveform and
    // shows up as a period anomaly even when no counter sees it.
    const SignalSpec a = SignalSpec::periodic(Wave::Sine, 354.1, 2, 0.5);
    const SignalSpec b = SignalSpec::periodic(Wave::Triangle, 1234.5, 1.5, 0.8);
    applyPsu(PSU_MIN_V);
    const SignalOutput out[2] = {applySignal(1, a), applySignal(2, b)};
    m_session->configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
    const bool check[2] = {true, true};
    const StreamCheckResult r = runStreamCheck(*m_session, m_soak_s, check, 4,
                                               [&](const std::string& line) { m_status("soak " + t + ": " + line); });
    const std::string json_path = ctx.path("captures", "stream_test.json");
    {
        std::ofstream f(ctx.absolute(json_path), std::ios::binary | std::ios::trunc);
        JsonWriter json(f);
        json.beginObject();
        json.field("transport", t);
        r.writeJson(json);
        json.endObject();
        json.finish();
    }
    ctx.artifact(json_path, "json", "stream check result");
    const double expected_frames = std::round(m_soak_s * 1000);
    ctx.measure("frames received intact", "", static_cast<double>(r.frames.ok), expected_frames,
                std::round(expected_frames * 0.99));
    ctx.measure("frames with a bad checksum", "", static_cast<double>(r.frames.bad_checksum), 0, std::nullopt, 0);
    ctx.measure("frames dropped", "", static_cast<double>(r.frames.dropped), 0, std::nullopt, 0);
    ctx.measure("frames without a checksum record (iso meta lost; not an error)", "",
                static_cast<double>(r.frames.unvalidated));
    ctx.measure("delivered sample rate", "S/s", r.delivered_sample_rate_hz, r.sample_rate_hz,
                r.sample_rate_hz * (1 - tol::SAMPLE_RATE), r.sample_rate_hz * (1 + tol::SAMPLE_RATE));
    ctx.measure("longest pause in the stream", "s", r.max_stall_s, std::nullopt, std::nullopt, tol::MAX_STALL_S);
    // A lost or repeated frame shifts both inputs at once (they share every frame), so the
    // check that catches what the frame counters miss counts anomalous periods on both inputs
    // within one period of each other. An anomaly on one input alone is a disturbance on that
    // input (on the QA tester CH2 picks one up now and then, with every frame intact).
    if (r.continuity.size() == 2)
    {
        const StreamContinuity& a = r.continuity[0];
        const StreamContinuity& b = r.continuity[1];
        const double window_s = (a.median_period_samples + b.median_period_samples) / r.sample_rate_hz;
        uint64_t both = 0;
        for (const double t1 : a.anomaly_at_s)
            for (const double t2 : b.anomaly_at_s)
                if (std::fabs(t1 - t2) <= window_s)
                {
                    both++;
                    break;
                }
        ctx.measure("periods off the median on both inputs at once (a lost or repeated frame)", "",
                    static_cast<double>(both), 0, std::nullopt, 0);
    }
    for (const StreamContinuity& c : r.continuity)
    {
        const std::string chs = "CH" + std::to_string(c.channel);
        const double f = out[c.channel - 1].frequency_hz;
        ctx.measure(chs + " periods more than 5% off the median (on this input alone, not judged)", "",
                    static_cast<double>(c.anomalies));
        ctx.measure(chs + " largest period deviation", "fraction", c.max_period_deviation);
        ctx.measure(chs + " median frequency", "Hz", c.frequency_hz, f, f * (1 - tol::FREQUENCY),
                    f * (1 + tol::FREQUENCY));
        if (!c.anomaly_times_s.empty())
        {
            std::string times;
            for (size_t i = 0; i < std::min<size_t>(5, c.anomaly_times_s.size()); i++)
                times += (i ? ", " : "") + num(std::round(c.anomaly_times_s[i] * 1000) / 1000);
            ctx.note(chs + " first anomalies at " + times + " s");
        }
    }
}

// --- firmware round trip ---------------------------------------------------
std::string firmwareImage(unsigned v, unsigned variant)
{
    char name[64];
    std::snprintf(name, sizeof name, "firmware/labrafirm_%04X_%02u.hex", v, variant);
    return getResourcePath(name);
}

void Suite::firmwareRoundTrip(TestContext& ctx)
{
    if (m_calibration_stored)
        ctx.skip("the board stores calibration, which the DFU erase in a flash would wipe; the "
                 "QA never changes a board's calibration");
    m_session.reset();
    const std::string old_image = firmwareImage(0x0007, 2);
    const std::string bundled = firmwareImage(EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
    const bool verbose = false;
    try
    {
        // 1. Down to 0x0007: librador reports 1 (flashed, but not the firmware it streams from).
        m_status("firmware.round_trip: flashing firmware 0x0007");
        {
            LibradorScope librador(&verbose, UsbTransport::Auto);
            const int connect = librador_connect();
            ctx.measure("before: connect result (0: current firmware)", "", connect, 0, 0, 0);
            const double t0 = nowSeconds();
            const int rc = librador_flash_firmware(old_image.c_str());
            ctx.measure("downgrade: flash time", "s", nowSeconds() - t0, std::nullopt, std::nullopt, 120);
            ctx.measure("downgrade: librador_flash_firmware result (1: runs other firmware)", "", rc, 1, 1, 1);
            ctx.measureText("downgrade: firmware after flashing",
                            version(librador_get_device_firmware_version(), librador_get_device_firmware_variant()),
                            version(0x0007, 2));
        }
        // 2. Back to the bundled image, the way the app's flash dialog does it.
        m_status("firmware.round_trip: flashing the bundled firmware back");
        {
            LibradorScope librador(&verbose, UsbTransport::Auto);
            const int connect = librador_connect();
            ctx.measure("old firmware: connect result (1: other firmware)", "", connect, 1, 1, 1);
            const double t0 = nowSeconds();
            const int rc = librador_flash_firmware(bundled.c_str());
            ctx.measure("update: flash time", "s", nowSeconds() - t0, std::nullopt, std::nullopt, 120);
            ctx.measure("update: librador_flash_firmware result (0: current firmware)", "", rc, 0, 0, 0);
            ctx.measureText("update: firmware after flashing",
                            version(librador_get_device_firmware_version(), librador_get_device_firmware_variant()),
                            version(EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT));
        }
        // 3. The re-flashed board streams.
        m_status("firmware.round_trip: streaming from the re-flashed board");
        {
            DeviceSession s(false, UsbTransport::Auto);
            s.configureScope(ScopeMode::Ch1Ch2, 4, SETTLE_S);
            const SampleWindow w = s.acquire(0.05);
            ctx.measure("after update: USB frames received in 50 ms", "", static_cast<double>(w.frames.ok),
                        std::nullopt, 1);
        }
    }
    catch (...)
    {
        ensureBundledFirmware(ctx);
        throw;
    }
    ensureBundledFirmware(ctx);
}

// Whatever happened, leave the board on the bundled firmware.
void Suite::ensureBundledFirmware(TestContext& ctx)
{
    const bool verbose = false;
    LibradorScope librador(&verbose, UsbTransport::Auto);
    bool bootloader = false, gobindar = false;
    if (librador_device_present_ex(&bootloader, &gobindar) && !bootloader && !gobindar &&
        librador_connect() == 0)
        return;
    const std::string bundled = firmwareImage(EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);
    ctx.note("the board was not on the bundled firmware after the round trip; flashing it back");
    const int rc = bootloader ? librador_bootloader_recover(bundled.c_str())
                              : librador_flash_firmware(bundled.c_str());
    if (rc != 0)
        ctx.fail("BOARD LEFT OFF THE BUNDLED FIRMWARE (recovery returned " + std::to_string(rc) +
                 "): it needs the Labrador app's flash dialog or `labrador-cli update-firmware` "
                 "before further use");
}

// Every hardware test and the firmware round trip, in the suite's fixed order, with the
// parameters of a run at `level`. A test that establishes a condition says so here
// (provide), because the plan adds a selected test's prerequisites from these declarations
// before anything runs; the Runner refuses a condition set by a test that does not declare it.
std::vector<HwTest> Suite::catalogue(Level level)
{
    std::vector<HwTest> tests;
    const auto provide = [&tests](const std::string& id, const std::string& title, const std::vector<std::string>& needs,
                                  const std::vector<std::string>& provides, TestFn fn) {
        const std::string group = id.rfind("firmware.", 0) == 0 ? "firmware" : "hw";
        tests.push_back({TestSpec{id, group, title, needs, provides}, std::move(fn)});
    };
    const auto add = [&provide](const std::string& id, const std::string& title, const std::vector<std::string>& needs,
                                TestFn fn) { provide(id, title, needs, {}, std::move(fn)); };
    const double soak_s = soakSeconds(level);

    const std::vector<std::string> board = {"board"};
    // "board" first, so a missing board is what an unmet test reports.
    const std::vector<std::string> sg1 = {"board", "sg1_ch1"}, sg2 = {"board", "sg2_ch2"},
                                   both = {"board", "sg1_ch1", "sg2_ch2"};

    provide("hw.board.present", "A Labrador board is attached, running the firmware this release needs", {}, board,
            [](Suite& s, TestContext& c) { s.boardPresent(c); });
    for (int ch = 1; ch <= 2; ch++)
        provide("hw.wiring.sg" + std::to_string(ch),
                "SG" + std::to_string(ch) + " is looped back to scope input CH" + std::to_string(ch) + " and nowhere else",
                board, {"sg" + std::to_string(ch) + "_ch" + std::to_string(ch)},
                [ch](Suite& s, TestContext& c) { s.wiring(c, ch); });
    for (int ch = 1; ch <= 2; ch++)
        add("hw.noise.ch" + std::to_string(ch),
            "CH" + std::to_string(ch) + " noise floor with the generators parked (gain 4)", board,
            [ch](Suite& s, TestContext& c) { s.noise(c, ch); });
    for (int ch = 1; ch <= 2; ch++)
    {
        for (const Wave w : WAVES)
        {
            add("hw.siggen.ch" + std::to_string(ch) + "." + waveName(w) + "_2237hz",
                "SG" + std::to_string(ch) + " " + waveName(w) + ", 2237 Hz, 0.5-2.5 V: frequency, levels and shape on CH" +
                    std::to_string(ch),
                ch == 1 ? sg1 : sg2, [ch, w](Suite& s, TestContext& c) { s.wave(c, ch, w, BASE_HZ); });
            add("hw.siggen.ch" + std::to_string(ch) + "." + waveName(w) + "_831hz",
                "SG" + std::to_string(ch) + " " + waveName(w) + ", 831 Hz, 0.5-2.5 V: frequency, levels and shape on CH" +
                    std::to_string(ch),
                ch == 1 ? sg1 : sg2, [ch, w](Suite& s, TestContext& c) { s.wave(c, ch, w, ODD_HZ); });
        }
        add("hw.siggen.ch" + std::to_string(ch) + ".sine_6vpp_x3",
            "SG" + std::to_string(ch) + " 6 Vpp 2237 Hz sine through the x3 output stage (PSU raised to 8 V)",
            ch == 1 ? sg1 : sg2, [ch](Suite& s, TestContext& c) { s.bigSine(c, ch, BASE_HZ); });
        add("hw.siggen.ch" + std::to_string(ch) + ".sine_6vpp_x3_831hz",
            "SG" + std::to_string(ch) + " 6 Vpp 831 Hz sine through the x3 output stage (PSU raised to 8 V)",
            ch == 1 ? sg1 : sg2, [ch](Suite& s, TestContext& c) { s.bigSine(c, ch, ODD_HZ); });
    }
    add("hw.siggen.dc_levels", "DC levels 0.25-6 V on both generators read back on both inputs", both,
        [](Suite& s, TestContext& c) { s.dcLevels(c); });
    for (const double g : {0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0})
    {
        add("hw.gain.x" + idNum(g), "Scope gain x" + num(g) + ": a 2237 Hz sine filling most of the range reads back right on both inputs",
            both, [g](Suite& s, TestContext& c) { s.gain(c, g, BASE_HZ); });
        add("hw.gain.x" + idNum(g) + "_831hz",
            "Scope gain x" + num(g) + ": an 831 Hz sine filling most of the range reads back right on both inputs", both,
            [g](Suite& s, TestContext& c) { s.gain(c, g, ODD_HZ); });
    }
    for (const double f : {10.0, 50.0, 100.0, 354.1, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0})
    {
        add("hw.sweep.f" + idNum(f) + "hz", "Frequency sweep: " + num(f) + " Hz sine on each generator in turn, frequency, levels and shape",
            both, [f](Suite& s, TestContext& c) { s.sweep(c, f); });
    }
    add("hw.mode.scope1", "Scope mode scope1 (CH1 only, 375 kS/s) with a 2237 Hz sine", sg1,
        [](Suite& s, TestContext& c) { s.modeScope1(c, BASE_HZ); });
    add("hw.mode.scope1_831hz", "Scope mode scope1 (CH1 only, 375 kS/s) with an 831 Hz sine", sg1,
        [](Suite& s, TestContext& c) { s.modeScope1(c, ODD_HZ); });
    add("hw.mode.scope1_750k", "Scope mode scope1-750k (CH1 only, 750 kS/s) with a 10 kHz sine", sg1,
        [](Suite& s, TestContext& c) { s.mode750k(c, 10000); });
    add("hw.mode.scope1_750k_831hz", "Scope mode scope1-750k (CH1 only, 750 kS/s) with an 831 Hz sine", sg1,
        [](Suite& s, TestContext& c) { s.mode750k(c, ODD_HZ); });
    for (const Wave w : {Wave::Sine, Wave::Square})
        for (int src = 1; src <= 2; src++)
            add("hw.isolation.sg" + std::to_string(src) + "_to_ch" + std::to_string(src == 1 ? 2 : 1) + "." +
                    waveName(w),
                std::string("Crosstalk from a 6 Vpp ") + waveName(w) + " on SG" + std::to_string(src) + " into the idle input",
                both, [src, w](Suite& s, TestContext& c) { s.isolation(c, src, w); });
    for (const double f : {BASE_HZ, ODD_HZ})
    {
        const std::string sfx = f == BASE_HZ ? "" : "_831hz";
        const std::string hz = f == BASE_HZ ? "2237 Hz" : "831 Hz";
        add("hw.trigger.ch1_rising" + sfx, "Trigger: CH1 rising through 1.5 V on a " + hz + " sine", sg1,
            [f](Suite& s, TestContext& c) { s.trigger(c, 1, true, 1.5, Wave::Sine, f); });
        add("hw.trigger.ch1_falling" + sfx, "Trigger: CH1 falling through 1.0 V on a " + hz + " sine", sg1,
            [f](Suite& s, TestContext& c) { s.trigger(c, 1, false, 1.0, Wave::Sine, f); });
        add("hw.trigger.ch2_rising_square" + sfx, "Trigger: CH2 rising through 1.5 V on a " + hz + " square", sg2,
            [f](Suite& s, TestContext& c) { s.trigger(c, 2, true, 1.5, Wave::Square, f); });
        add("hw.trigger.ch2_falling_triangle" + sfx, "Trigger: CH2 falling through 2.0 V on a " + hz + " triangle", sg2,
            [f](Suite& s, TestContext& c) { s.trigger(c, 2, false, 2.0, Wave::Triangle, f); });
        add("hw.trigger.placement" + sfx,
            "Trigger window placement on a " + hz + " sine: all-post, mostly-pre and long windows", sg1,
            [f](Suite& s, TestContext& c) { s.triggerPlacement(c, f); });
    }
    add("hw.trigger.timeout_unreachable", "A trigger level above the signal times out cleanly", sg1,
        [](Suite& s, TestContext& c) { s.triggerTimeout(c); });
    add("hw.trigger.no_false_trigger_on_dc", "A DC level on the trigger level does not trigger on noise", sg1,
        [](Suite& s, TestContext& c) { s.triggerDc(c); });
    for (const char* t : TRANSPORTS)
    {
        const std::string ts = t;
        provide("hw.transport." + ts + ".measure",
                "USB transport " + ts + ": connects and measures a 2237 Hz sine on CH1 and a 4474 Hz triangle on CH2",
                both, {"transport_" + ts},
                [ts](Suite& s, TestContext& c) { s.transportMeasure(c, ts, BASE_HZ, true); });
        add("hw.transport." + ts + ".measure_831hz",
            "USB transport " + ts + ": an 831 Hz sine on CH1 and an 831 Hz triangle on CH2",
            {"board", "sg1_ch1", "sg2_ch2", "transport_" + ts},
            [ts](Suite& s, TestContext& c) { s.transportMeasure(c, ts, ODD_HZ, false); });
        add("hw.transport." + ts + ".soak",
            "USB transport " + ts + ": " + num(soak_s) + " s stream with no lost, corrupt or late frames",
            {"board", "transport_" + ts}, [ts](Suite& s, TestContext& c) { s.transportSoak(c, ts); });
    }
    add("firmware.round_trip", "Flash firmware 0x0007, then the bundled image back, through the DFU bootloader",
        {"board"}, [](Suite& s, TestContext& c) { s.firmwareRoundTrip(c); });
    return tests;
}

void Suite::declareConditions(Runner& runner)
{
    runner.declareCondition("board", "no usable board (see hw.board.present)", true);
    for (const char* c : {"sg1_ch1", "sg2_ch2"})
        runner.declareCondition(c, std::string("hw.wiring did not confirm the ") + c + " loopback", true);
    for (const char* t : TRANSPORTS)
        runner.declareCondition(std::string("transport_") + t,
                                std::string("hw.transport.") + t + ".measure did not pass", false);
}

void Suite::startIteration()
{
    m_session.reset();
    m_calibration_stored = false;
}

void Suite::run(const std::vector<PlannedTest>& planned)
{
    for (const PlannedTest& p : planned)
    {
        const auto it = std::find_if(m_tests.begin(), m_tests.end(),
                                     [&p](const HwTest& t) { return t.spec.id == p.test.id; });
        if (it == m_tests.end())
            throw std::logic_error("the plan holds " + p.test.id + ", which is not a hardware or firmware test");
        m_status(p.test.id);
        const TestFn& fn = it->fn;
        m_runner.run(p.test, [this, &fn](TestContext& c) { fn(*this, c); });
    }
    // The next phase, or the app, opens its own session.
    m_session.reset();
}

std::vector<TestSpec> specsOfGroup(Level level, const std::string& group)
{
    std::vector<TestSpec> specs;
    for (const HwTest& t : Suite::catalogue(level))
        if (t.spec.group == group)
            specs.push_back(t.spec);
    return specs;
}
} // namespace

std::vector<TestSpec> hardwareTestSpecs(Level level)
{
    return specsOfGroup(level, "hw");
}

std::vector<TestSpec> firmwareTestSpecs(Level level)
{
    return specsOfGroup(level, "firmware");
}

struct HardwareTests::Impl
{
    Suite suite;
};

HardwareTests::HardwareTests(Runner& runner, std::string out_dir, Level level,
                             std::function<void(const std::string&)> status)
    : m_impl(new Impl{Suite(runner, std::move(out_dir), level, std::move(status))})
{
    Suite::declareConditions(runner);
}

HardwareTests::~HardwareTests() = default;

void HardwareTests::startIteration()
{
    m_impl->suite.startIteration();
}

void HardwareTests::run(const std::vector<PlannedTest>& planned)
{
    m_impl->suite.run(planned);
}
} // namespace autoqa

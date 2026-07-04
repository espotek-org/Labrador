#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <algorithm>  // std::min, std::max, std::clamp
#include <cstdint>
#include <thread>
#include <limits>
#include <cstdio>

#include "librador.h"
#include "implot.h"

class NetworkAnalyser {
public:
    struct Config {
        // Sweep
        double f_start = 1;
        double f_stop = 1e3;
        int    points = 100;
        bool   log_spacing = true;

        // Lock-in / timing
        double IFBW_Hz = 1e3;
        double min_settle_s = 0.00025;
        int    settle_cycles = 1;
        int    dwell_cycles = 4;

        // Optional IFBW ? limits; OFF by default
        bool   use_ifbw_limits = false;
        double dwell_tau_mult = 4.0;
        double settle_tau_mult = 3.0;

        // UI / frame loop period (ETA one-frame minimum at WaitSettle)
        double ui_frame_period_s = 0.020; // ~20 ms typical

        // Sampling policy
        bool   auto_sample_rate = true;
        double fs_min = 5e3;
        double fs_max = 375e3;
        size_t max_samples_per_record = 10000;
        int    min_samples_per_cycle = 12;

        // I/O
        int    ch_input = 1;
        int    ch_output = 2;
        double fs_adc_hint = 375000.0;

        // Generator
        int    gen_channel = 1;
        double gen_amplitude_v = 3.0;  // hard cap
        double gen_offset_v = 0.0;

        // Amplitude shaping: [freq, amplitude] step points
        bool amp_shaping_enable = true;
        std::vector<std::pair<double, double>> amp_points = {
            {1.0, 1.0}, {10.0, 2.0}, {30.0, 3.0}
        };

        // Prefilter (Savitzky-Golay, quadratic)
        bool   denoise_enable = true;
        double denoise_frac_cycle = 0.15; // window ~= frac * samples-per-cycle (5..21, odd)

        // Coherence gate
        double coherence_min = 0.65;

        // Display / safety
        double mag_floor_dB = -100.0;
    };

    // ===== ETA & Button Label (matches runtime) =====
    std::string AcquireButtonLabelFromUI(const Config& ui_cfg, const char* base = "Acquire") const {
        char eta[32];
        std::snprintf(eta, sizeof(eta), "%.f seconds", EstimateSweepSeconds_UI(ui_cfg));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s (est. %s)", base, eta);
        return std::string(buf);
    }

    // ETA: per point waits settle+dwell, with a ONE-FRAME minimum at WaitSettle.
    double EstimateSweepSeconds_UI(const Config& c) const {
        if (c.points <= 0) return 0.0;
        const double dt = std::max(1e-6, c.ui_frame_period_s);

        auto settle_term = [&](double f) {
            const double by_cycles = std::max(1, c.settle_cycles) / f;
            double s = std::max(by_cycles, c.min_settle_s);
            if (c.use_ifbw_limits) {
                const double tau = 1.0 / (2.0 * M_PI * c.IFBW_Hz);
                s = std::max(s, c.settle_tau_mult * tau);
            }
            return s;
        };
        auto dwell_term = [&](double f) {
            const double by_cycles = std::max(1, c.dwell_cycles) / f;
            if (!c.use_ifbw_limits) return by_cycles;
            const double tau = 1.0 / (2.0 * M_PI * c.IFBW_Hz);
            return std::max(by_cycles, c.dwell_tau_mult * tau);
        };

        double total = 0.0;
        if (c.log_spacing) {
            const double n_1 = (double)std::max(1, c.points - 1);
            const double r = std::pow(c.f_stop / c.f_start, 1.0 / n_1);
            double f = c.f_start;
            for (int i = 0; i < c.points; ++i, f *= r) {
                const double t_wait_cont = settle_term(f) + dwell_term(f);
                total += std::max(dt, t_wait_cont);
            }
        }
        else {
            const double n_1 = (double)std::max(1, c.points - 1);
            const double df = (c.f_stop - c.f_start) / n_1;
            for (int i = 0; i < c.points; ++i) {
                const double f = c.f_start + i * df;
                const double t_wait_cont = settle_term(f) + dwell_term(f);
                total += std::max(dt, t_wait_cont);
            }
        }
		total += 4.0+4*total/30; // estimated overhead
        return total;
    }

    // Readonly for plotting
    std::vector<double> freqs() const {
        const int n = std::clamp(idx_ + (state_ == State::Done ? 0 : 0), 0, (int)freqs_.size());
        return std::vector<double>(freqs_.begin(), freqs_.begin() + n);
    }

    std::vector<double> mag_linear() const {
        const int n = std::clamp(idx_, 0, (int)mag_.size());
        return std::vector<double>(mag_.begin(), mag_.begin() + n);
    }

    std::vector<double> mag_dB() const {
        const int n = std::clamp(idx_, 0, (int)mag_dB_.size());
        return std::vector<double>(mag_dB_.begin(), mag_dB_.begin() + n);
    }

    std::vector<double> phase_rad() const {
        const int n = std::clamp(idx_, 0, (int)ph_.size());
        return std::vector<double>(ph_.begin(), ph_.begin() + n);
    }


    bool done() const { return state_ == State::Done || state_ == State::Idle; }
    bool running() const {
        return state_ == State::SetTone || state_ == State::WaitSettle ||
            state_ == State::Capture || state_ == State::NextPoint;
    }
    int  current_index() const { return idx_; }

    const Config& CurrentConfig() const { return cfg_; }

    // Lifecycle
    void Reset() {
        freqs_.clear(); mag_.clear(); mag_dB_.clear(); ph_.clear();
        plan_.clear();
        state_ = State::Idle; idx_ = -1;
        fs_adc_nominal_ = 0.0;
        fs_eff_point_ = 0.0;
    }

    void StartSweep(const Config& cfg) {
        cfg_ = cfg;
        Reset();

        // Sort & dedup amplitude breakpoints
        if (!cfg_.amp_points.empty()) {
            std::sort(cfg_.amp_points.begin(), cfg_.amp_points.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<std::pair<double, double>> dedup;
            for (const auto& p : cfg_.amp_points) {
                if (dedup.empty() || p.first != dedup.back().first) dedup.push_back(p);
                else dedup.back().second = p.second;
            }
            cfg_.amp_points.swap(dedup);
        }

        fs_adc_nominal_ = get_device_fs_adc_();
        if (fs_adc_nominal_ <= 0.0) fs_adc_nominal_ = cfg_.fs_adc_hint;

        make_sweep_(cfg_.f_start, cfg_.f_stop, cfg_.points, cfg_.log_spacing);

        mag_.assign(plan_.size(), 0.0);
        ph_.assign(plan_.size(), 0.0);
        mag_dB_.assign(plan_.size(), 0.0);

        idx_ = 0;
        state_ = State::SetTone;
    }

    // ====== MULTI-ADVANCE TICK ======
    void Tick() {
        // Advance through as many non-waiting states as possible in this call.
        // Only WaitSettle may cause us to return early (single-frame pause).
        for (int steps = 0; steps < 16; ++steps) { // guard
            if (state_ == State::Idle || state_ == State::Done)
                return;

            switch (state_) {
            case State::SetTone: {
                const auto& p = plan_[idx_];

                const double a_eff = std::min(amp_for_freq_(p.f), cfg_.gen_amplitude_v);
                librador_send_sin_wave(cfg_.gen_channel, p.f, a_eff, cfg_.gen_offset_v);
                coarse_sleep_ms_(3); // generator command overhead

                // Compute waits for this point
                t_settle_s_ = settle_time_(p.f);
                t_dwell_s_ = dwell_time_(p.f);
                t_wait_total_s_ = t_settle_s_ + t_dwell_s_; // wait settle + dwell before capture

                fs_eff_point_ = choose_sample_rate_(p.f, t_dwell_s_); // <<< present
                t_wait_begin_ = Clock::now();
                state_ = State::WaitSettle;
            } continue;

            case State::WaitSettle: {
                const double elapsed = sec_since_(t_wait_begin_);
                if (elapsed < t_wait_total_s_) {
                    // Not yet: accept a single-frame delay. Return without advancing further.
                    return;
                }
                // Time elapsed; advance this frame into Capture
                state_ = State::Capture;
            } continue;

            case State::Capture: {
                const auto& p = plan_[idx_];

                // Single capture per point
                auto* xin = librador_get_analog_data(cfg_.ch_input, t_dwell_s_, fs_eff_point_, 0.0, 0);
                auto* yout = librador_get_analog_data(cfg_.ch_output, t_dwell_s_, fs_eff_point_, 0.0, 0);

                if (!xin || !yout || xin->empty() || yout->empty() || xin->size() != yout->size()) {
                    mag_[idx_] = std::numeric_limits<double>::quiet_NaN();
                    mag_dB_[idx_] = std::numeric_limits<double>::quiet_NaN();
                    ph_[idx_] = std::numeric_limits<double>::quiet_NaN();
                    state_ = State::NextPoint;
                    continue;
                }

                // Optional SG prefilter
                if (cfg_.denoise_enable) {
                    const double spc = fs_eff_point_ / std::max(1e-9, p.f);
                    int win = (int)std::round(std::clamp(cfg_.denoise_frac_cycle * spc, 5.0, 21.0));
                    if (win % 2 == 0) ++win;
                    if (spc >= 10.0) {
                        savgol_q2_filter_inplace_(*xin, win);
                        savgol_q2_filter_inplace_(*yout, win);
                    }
                }

                // Lock-in on single window
                IQ X{}, Y{};
                const double omega = 2.0 * M_PI * p.f / fs_eff_point_;
                lockin_accumulate_demean_(*xin, omega, 0.0, X);
                lockin_accumulate_demean_(*yout, omega, 0.0, Y);

                const double N = (double)xin->size();
                if (N > 0.0) {
                    X.I /= N; X.Q /= N; Y.I /= N; Y.Q /= N;

                    const double Xr = X.I, Xi = X.Q;
                    const double Yr = Y.I, Yi = Y.Q;
                    const double Sxx = (Xr * Xr + Xi * Xi);
                    const double Syy = (Yr * Yr + Yi * Yi);
                    const double Sxy_re = (Yr * Xr + Yi * Xi);
                    const double Sxy_im = (Yi * Xr - Yr * Xi);

                    const double eps = 1e-30;
                    const double coh_num = Sxy_re * Sxy_re + Sxy_im * Sxy_im;
                    const double coh_den = (Sxx * Syy) + eps;
                    const double gamma2 = coh_num / coh_den;

                    const double H_re = Sxy_re / (Sxx + eps);
                    const double H_im = Sxy_im / (Sxx + eps);
                    const double m = std::hypot(H_re, H_im);
                    const double a = std::atan2(H_im, H_re);

                    const double m_floor = std::pow(10.0, cfg_.mag_floor_dB / 20.0);
                    const double m_safe = std::max(m, m_floor);
                    const bool   bad = (!std::isfinite(gamma2)) || gamma2 < cfg_.coherence_min;

                    mag_[idx_] = m;
                    mag_dB_[idx_] = bad ? std::numeric_limits<double>::quiet_NaN()
                        : 20.0 * std::log10(m_safe);
                    ph_[idx_] = bad ? std::numeric_limits<double>::quiet_NaN() : a;
                }
                else {
                    mag_[idx_] = std::numeric_limits<double>::quiet_NaN();
                    mag_dB_[idx_] = std::numeric_limits<double>::quiet_NaN();
                    ph_[idx_] = std::numeric_limits<double>::quiet_NaN();
                }

                state_ = State::NextPoint;
            } continue;

            case State::NextPoint: {
                idx_++;
                if (idx_ >= (int)plan_.size()) {
                    state_ = State::Done;
                    return;
                }
                else {
                    state_ = State::SetTone; // advance immediately this frame
                }
            } continue;

            default: return;
            }
        }
    }

private:
    using Clock = std::chrono::steady_clock;

    struct PointPlan { double f; };
    struct IQ { double I = 0.0, Q = 0.0; };

    enum class State { Idle, SetTone, WaitSettle, Capture, NextPoint, Done };

    Config cfg_;
    State  state_ = State::Idle;

    std::vector<PointPlan> plan_;
    std::vector<double> freqs_, mag_, mag_dB_, ph_;

    int    idx_ = -1;

    double fs_adc_nominal_ = 0.0;
    double fs_eff_point_ = 0.0;

    double t_settle_s_ = 0.0, t_dwell_s_ = 0.0, t_wait_total_s_ = 0.0;
    Clock::time_point t_wait_begin_{};

    // ===== timing models (we use “settle + dwell” before capture) =====
    double dwell_time_(double f) const {
        const double by_cycles = std::max(1, cfg_.dwell_cycles) / f;
        if (!cfg_.use_ifbw_limits) return by_cycles;
        const double tau = 1.0 / (2.0 * M_PI * cfg_.IFBW_Hz);
        const double by_tau = cfg_.dwell_tau_mult * tau;
        return std::max(by_cycles, by_tau);
    }
    double settle_time_(double f) const {
        const double by_cycles = std::max(1, cfg_.settle_cycles) / f;
        if (!cfg_.use_ifbw_limits) return std::max(by_cycles, cfg_.min_settle_s);
        const double tau = 1.0 / (2.0 * M_PI * cfg_.IFBW_Hz);
        const double by_tau = cfg_.settle_tau_mult * tau;
        return std::max({ by_cycles, by_tau, cfg_.min_settle_s });
    }

    // ===== hardware/flow helpers =====
    static double sec_since_(Clock::time_point t0) {
        using namespace std::chrono;
        return duration<double>(Clock::now() - t0).count();
    }
    static void coarse_sleep_ms_(int ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    double get_device_fs_adc_() {
        return cfg_.fs_adc_hint;
    }
    void make_sweep_(double f0, double f1, int n, bool logsp) {
        freqs_.resize(n);
        plan_.resize(n);
        if (logsp) {
            const double r = std::pow(f1 / f0, 1.0 / (n - 1));
            double f = f0;
            for (int i = 0; i < n; ++i, f *= r) { freqs_[i] = f; plan_[i] = { f }; }
        }
        else {
            const double df = (f1 - f0) / (n - 1);
            for (int i = 0; i < n; ++i) {
                const double f = f0 + i * df;
                freqs_[i] = f; plan_[i] = { f };
            }
        }
    }
    double choose_sample_rate_(double f, double dwell_s) const {
        if (!cfg_.auto_sample_rate)
            return std::clamp(fs_adc_nominal_, cfg_.fs_min, cfg_.fs_max);

        const double fs_budget_max = (cfg_.max_samples_per_record > 0)
            ? (double)cfg_.max_samples_per_record / std::max(1e-9, dwell_s)
            : cfg_.fs_max;

        const double fs_min_cycle = std::max(1, cfg_.min_samples_per_cycle) * f;

        double fs_target = std::max(cfg_.fs_min, fs_min_cycle);
        fs_target = std::min(fs_target, fs_budget_max);
        fs_target = std::min(fs_target, cfg_.fs_max);
        fs_target = std::min(fs_target,
            fs_adc_nominal_ > 0.0 ? fs_adc_nominal_ : cfg_.fs_max);
        return std::max(fs_target, cfg_.fs_min);
    }
    double amp_for_freq_(double f) const {
        if (!cfg_.amp_shaping_enable || cfg_.amp_points.empty())
            return cfg_.gen_amplitude_v;

        double a = cfg_.amp_points.front().second;
        for (const auto& bp : cfg_.amp_points) {
            if (f >= bp.first) a = bp.second;
            else break;
        }
        return a;
    }

    // Lock-in & filtering helpers
    static void lockin_accumulate_demean_(const std::vector<double>& sig, double omega, double phi0, IQ& out) {
        if (sig.empty()) return;

        double mean = 0.0;
        for (double v : sig) mean += v;
        mean /= sig.size();

        double c = std::cos(phi0), s = std::sin(phi0);
        const double dc = std::cos(omega), ds = std::sin(omega);

        double Scc = 0.0, Sss = 0.0;
        for (double v : sig) {
            const double x = v - mean;
            out.I += x * c;
            out.Q += x * s;
            Scc += c * c;
            Sss += s * s;

            const double c_next = c * dc - s * ds;
            const double s_next = c * ds + s * dc;
            c = c_next; s = s_next;
        }
        out.I /= (Scc + 1e-30);
        out.Q /= (Sss + 1e-30);
    }
    static void savgol_q2_filter_inplace_(std::vector<double>& x, int window) {
        if (window < 5) return;
        if (window % 2 == 0) ++window;
        const int N = (int)x.size();
        if (N < window) return;

        std::vector<double> w(window);
        gen_savgol_q2_weights_(window, w.data());

        std::vector<double> y(N);
        const int half = window / 2;

        for (int i = half; i < N - half; ++i) {
            double s = 0.0;
            for (int k = -half; k <= half; ++k) s += w[k + half] * x[i + k];
            y[i] = s;
        }
        for (int i = 0; i < half; ++i) y[i] = x[i];
        for (int i = N - half; i < N; ++i) y[i] = x[i];

        x.swap(y);
    }
    static void gen_savgol_q2_weights_(int window, double* w) {
        const int m = window / 2;
        double S0 = (double)window;
        double S2 = 0.0, S4 = 0.0;
        for (int k = -m; k <= m; ++k) { S2 += (double)k * (double)k; S4 += (double)k * (double)k * (double)k * (double)k; }

        const double detA = S0 * (S2 * S4) - S2 * (S2 * S2);
        const double a00 = (S2 * S4) / detA;
        const double a02 = -(S2 * S2) / detA;

        double sumw = 0.0;
        for (int k = -m; k <= m; ++k) {
            const double kk = (double)k * (double)k;
            w[k + m] = a00 + a02 * kk;
            sumw += w[k + m];
        }
        const double inv = 1.0 / sumw;
        for (int i = 0; i < window; ++i) w[i] *= inv;
    }
};

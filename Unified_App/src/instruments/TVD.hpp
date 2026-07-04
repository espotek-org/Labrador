// Author: ChatGPT (Condat's fast 1-D TV denoising, adapted for your project)
// Exact solver for:  min_u  0.5*||u - f||_2^2 + lambda * TV(u)
// Runs in O(n) time, no iterations. Suitable for per-frame use.

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

static inline std::vector<double>
tv_denoise(const std::vector<double>& f, double lambda) {
    const int n = (int)f.size();
    if (n <= 1 || lambda <= 0.0) return f;

    std::vector<double> x(n);

    // Variables follow the notation of:
    // L. Condat, "A direct algorithm for 1D total variation denoising," IEEE SPL, 2013.
    int k = 0;       // current index
    int k0 = 0;      // beginning of the current segment
    double umin = lambda;
    double umax = -lambda;
    double vmin = f[0] - lambda;
    double vmax = f[0] + lambda;

    // The "taut string" like sweeping
    for (int i = 1; i < n; ++i) {
        double val = f[i];
        // Extend the lower and upper piecewise-linear envelopes
        umin += val - vmin;
        umax += val - vmax;

        if (umin <= -lambda) {
            // Lower envelope violated: output a segment at (vmin)
            for (; k0 <= k; ++k0) x[k0] = vmin;
            k = k0 = i - 1;
            vmin = f[k0];
            vmax = f[k0] + 2.0 * lambda;
            umin = lambda;
            umax = -lambda;
        }
        else if (umax >= lambda) {
            // Upper envelope violated: output a segment at (vmax)
            for (; k0 <= k; ++k0) x[k0] = vmax;
            k = k0 = i - 1;
            vmax = f[k0];
            vmin = f[k0] - 2.0 * lambda;
            umin = lambda;
            umax = -lambda;
        }
        else {
            // No violation: keep envelopes tight
            if (umin >= lambda) {
                vmin += (umin - lambda) / (i - k0 + 1);
                umin = lambda;
            }
            if (umax <= -lambda) {
                vmax += (umax + lambda) / (i - k0 + 1);
                umax = -lambda;
            }
        }
        ++k;
    }

    // Final flush: choose any value in [vmin, vmax] (they’re within 2*lambda)
    double v = 0.5 * (vmin + vmax);
    for (; k0 < n; ++k0) x[k0] = v;
    return x;
}

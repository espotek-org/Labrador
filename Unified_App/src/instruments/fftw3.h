// Drop-in replacement for the FFTW3 subset the instrument code uses, backed
// by pocketfft (BSD-3, vendored in deps/). Lets the ported Monash widgets keep
// their fftw call sites while avoiding the FFTW build burden on
// Android/Windows/Pi. Transforms are unnormalised, matching FFTW semantics.
#pragma once

#include "pocketfft_hdronly.h"

#include <complex>
#include <cstdlib>

typedef double fftw_complex[2];

#define FFTW_ESTIMATE 0

struct fftw_plan_s
{
    int n;
    bool forward;
    double* rin;
    fftw_complex* cout;
    fftw_complex* cin;
    double* rout;
};
typedef fftw_plan_s* fftw_plan;

inline void* fftw_malloc(size_t sz) { return std::malloc(sz); }
inline void fftw_free(void* p) { std::free(p); }
inline fftw_complex* fftw_alloc_complex(size_t n)
{
    return (fftw_complex*)std::malloc(n * sizeof(fftw_complex));
}

inline fftw_plan fftw_plan_dft_r2c_1d(int n, double* in, fftw_complex* out, unsigned)
{
    return new fftw_plan_s { n, true, in, out, nullptr, nullptr };
}

inline fftw_plan fftw_plan_dft_c2r_1d(int n, fftw_complex* in, double* out, unsigned)
{
    return new fftw_plan_s { n, false, nullptr, nullptr, in, out };
}

inline void fftw_execute(fftw_plan p)
{
    pocketfft::shape_t shape { (size_t)p->n };
    pocketfft::stride_t stride_r { sizeof(double) };
    pocketfft::stride_t stride_c { sizeof(std::complex<double>) };
    pocketfft::shape_t axes { 0 };
    if (p->forward)
        pocketfft::r2c(shape, stride_r, stride_c, axes, pocketfft::FORWARD, p->rin,
            reinterpret_cast<std::complex<double>*>(p->cout), 1.0);
    else
        pocketfft::c2r(shape, stride_c, stride_r, axes, pocketfft::BACKWARD,
            reinterpret_cast<std::complex<double>*>(p->cin), p->rout, 1.0);
}

inline void fftw_destroy_plan(fftw_plan p) { delete p; }

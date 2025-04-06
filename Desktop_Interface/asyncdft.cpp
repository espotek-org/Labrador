#include "asyncdft.h"
#include <cmath>
#include <omp.h>


AsyncDFT::AsyncDFT()
{
    /*FFTW3 inits*/
    fftw_init_threads();
    fftw_plan_with_nthreads(omp_get_max_threads() * 2);
    out_buffer = fftw_alloc_complex(n_samples);
    plan = fftw_plan_dft_r2c_1d(n_samples,in_buffer, out_buffer,0);
}

AsyncDFT::~AsyncDFT()
{
}

QVector<double> AsyncDFT::getPowerSpectrum_dBmV(QVector<double> input, double wind_fact_sum)
{
    /*Before doing anything, check if sliding DFT is computable*/
    if (input.size() < n_samples) {
        return QVector<double>();
    }

    for(int i = 0; i < n_samples; i++) {
        in_buffer[i] = input[i];
    }

    /*Zero-padding for better resolution of DFT*/
    QVector<double> amplitude(n_samples/2+1,0);

    /*Executing FFTW plan*/
    fftw_execute(plan);

    /* dBmV = 20*log10(|V_fft,mv/N|) - wind_corr
       dBmV = 20*log10(|V_fft,mv/N|) - 20*log10(∑(Wi)/N)
       dBmV = 20*log10((10^3) * |V_fft| / N) - 20*log10(∑(Wi) / N)
       dBmV = 20*(log10(10^3)) + 20*log10(|V_fft|) - 20*log10(N)) - 20*log10(∑Wi) + 20*log10(N)
       dBmV = 60 + 20*log10(|V_fft|)) - 20*log10(∑Wi)
       dBmV = 60 + 10*log10(|V_fft|^2) - 20*log10(∑Wi)
    */
    for (int k = 0; k <= (n_samples+1)/2; ++k) {
         amplitude[k] = 60 + 10*std::log10(out_buffer[k][0]*out_buffer[k][0] + out_buffer[k][1]*out_buffer[k][1]) - 20*std::log10(wind_fact_sum);
    }

    return amplitude;
}

QVector<double> AsyncDFT::getFrequencyWindow(int samplesPerSeconds)
{
    double delta_freq = ((double)  samplesPerSeconds)/ ((double) n_samples);
    QVector<double> f(n_samples/2 + 1);

    for (int i = 0; i < n_samples/2 + 1; i++) {
        f[i] = i*delta_freq;
    }

    return f;
}

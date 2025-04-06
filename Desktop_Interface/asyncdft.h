#ifndef ASYNCDFT_H
#define ASYNCDFT_H
#include <QVector>
#include <fftw3.h>

class AsyncDFT
{
public:
    AsyncDFT();
    ~AsyncDFT();
    static const int n_samples = 1<<17;

    QVector<double> getPowerSpectrum_dBmV(QVector<double> input, double wind_fact_sum);
    QVector<double> getFrequencyWindow(int samplesPerSeconds);

private:
    double in_buffer[n_samples];
    /*FFTW3*/
    fftw_plan plan;
    fftw_complex *out_buffer;
};

#endif // ASYNCDFT_H

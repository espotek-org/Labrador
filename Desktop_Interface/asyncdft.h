#ifndef ASYNCDFT_H
#define ASYNCDFT_H
#include <QVector>
#include <list>
#include <memory>
#include <fftw3.h>

class AsyncDFT
{
public:
    AsyncDFT();
    ~AsyncDFT();
    static const int n_samples = 1<<17;

    /* Raise exception if not ready yet*/
    QVector<double> getPowerSpectrum_dBmV(QVector<double> input, double wind_fact_sum);
    QVector<double> getFrequencyWindow(int samplesPerSeconds);

    /*Add a sample to the time domain samples*/
    void addSample(short sample);

    /*Clear the window*/
    void clearWindow();

    /*Return the window of samples*/
    std::unique_ptr<short[]> getWindow();

private:
    /*Time domain window*/
    std::list<double> window;
    double in_buffer[n_samples];
    /*FFTW3*/
    fftw_plan plan;
    fftw_complex *out_buffer;
};

#endif // ASYNCDFT_H

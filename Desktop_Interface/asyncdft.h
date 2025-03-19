#ifndef ASYNCDFT_H
#define ASYNCDFT_H
#include <thread>
#include <QVector>
#include <mutex>
#include <queue>
#include <list>
#include <fftw3.h>

class AsyncDFT
{
public:
    AsyncDFT();
    ~AsyncDFT();
    static const int n_samples = 1<<17;

    /* Raise exception if not ready yet*/
    QVector<double> getPowerSpectrum(QVector<double> input);
    QVector<double> getFrequenciyWindow(int samplesPerSeconds);

    /*Add a sample to the time domain samples*/
    void addSample(short sample);

    /*Clear the window*/
    void clearWindow();

    /*Return the window of samples*/
    std::unique_ptr<short[]> getWindow();

    /*Normalize the DFT*/
    QVector<double> normalizeDFT(double e_maximum, QVector<double> dft);
private:
    /*Thread manager method*/
    void threadManager(); //threaded

    /*Shifts left the window by 1*/
    void shift();

public:
    /*Maximum power value obtained from power spectrum*/
    double maximum = -1;
private:
    /*Time domain window*/
    std::list<double> window;
    double in_buffer[n_samples];
    /*Indicates if dft is available*/
    bool data_valid;
    /*Number of time domain samples accumulated*/
    int samples_count;
    /*FFTW3*/
    fftw_plan plan;
    fftw_complex *out_buffer;
    std::mutex mtx_samples;
    bool stopping = false;
    std::thread manager;
    std::queue<short> pending_samples;
};

#endif // ASYNCDFT_H

#include "asyncdft.h"
#include <iostream>
#include <math.h>
#include <omp.h>
#include "isobuffer.h"
#define DBG 0


AsyncDFT::AsyncDFT()
{
    /*Creating the main thread, which will manage everything*/
    stopping = false;
    /*Data is not valid until we get n_samples into the window*/
    data_valid = false;
    /*Samples counter*/
    samples_count=0;
    /*Initializing time domain window to 0s*/
    /*FFTW3 inits*/
    fftw_init_threads();
    fftw_plan_with_nthreads(omp_get_max_threads() * 2);
#if DBG
    std::cout << "Starting with " << omp_get_max_threads() << "threads" << std::endl;
#endif
    out_buffer = fftw_alloc_complex(n_samples);
    plan = fftw_plan_dft_r2c_1d(n_samples,in_buffer, out_buffer,0);
}

AsyncDFT::~AsyncDFT()
{
#if DBG
    stopping = true;
    mtx_samples.unlock();   //Unlock thread manager if blocked and waiting for more samples
    while (!manager.joinable());
    manager.join();
    std::cout << "Joined manager thread [DESTRUCTOR]" << std::endl;
#endif
}

void AsyncDFT::threadManager()
{
    while(stopping == false) {
        /*Calculating DFT if there are new samples, otherwise DFT would be the same*/
        if (samples_count >= n_samples) {
            mtx_samples.lock();
            if (!window.empty()) {
                window.pop_front();
            }
            short tmp = pending_samples.front();
            pending_samples.pop();
            window.push_back(tmp);
            /*Data is now valid*/
            data_valid = true;
            mtx_samples.unlock();
        }
    }
}


void AsyncDFT::addSample(short sample)
{
    /*Adding to the waiting jobs the sample*/
    if (samples_count >= n_samples) {
        /*Shifting window by 1 by removing first element and adding an element to the end*/
        window.pop_front();
        window.push_back(sample);
        samples_count = n_samples;
        data_valid = true;
    } else {
        /*Fill the window*/
        window.push_back(sample);
    }
    /*Updating the number of samples*/
    samples_count++;
}

void AsyncDFT::clearWindow()
{
    window.clear();
    samples_count = 0;
}

QVector<double> AsyncDFT::getPowerSpectrum(QVector<double> input)
{
    /*Before doing anything, check if sliding DFT is computable*/
    if (data_valid == false) {
        throw std::exception();
    }

    for(int i = 0; i < n_samples; i++) {
        in_buffer[i] = input[i];
    }

    /*Zero-padding for better resolution of DFT*/
    QVector<double> amplitude(n_samples/2+1,0);
    maximum = -1;

    /*Executing FFTW plan*/
    fftw_execute(plan);
    amplitude[0] = sqrt(out_buffer[0][0]*out_buffer[0][0] + out_buffer[0][1]*out_buffer[0][1]);  /* DC component */

    maximum = (amplitude[0] > maximum ) ? amplitude[0] : maximum;

    for (int k = 1; k < (n_samples+1)/2; ++k) {  /* (k < N/2 rounded up) */
         amplitude[k] = sqrt(out_buffer[k][0]*out_buffer[k][0] + out_buffer[k][1]*out_buffer[k][1]);

         maximum = (amplitude[k] > maximum ) ? amplitude[k] : maximum;
    }
    if (n_samples % 2 == 0) { /* N is even */
         amplitude[n_samples/2] = sqrt(out_buffer[n_samples/2][0]*out_buffer[n_samples/2][0]);  /* Nyquist freq. */

         maximum = (amplitude[n_samples/2] > maximum ) ? amplitude[n_samples/2] : maximum;

    }

    return amplitude;
}

QVector<double> AsyncDFT::getFrequenciyWindow(int samplesPerSeconds)
{
    double delta_freq = ((double)  samplesPerSeconds)/ ((double) n_samples);
    QVector<double> f(n_samples/2 + 1);

    for (int i = 0; i < n_samples/2 + 1; i++) {
        f[i] = i*delta_freq;
    }

    return f;
}

std::unique_ptr<short[]> AsyncDFT::getWindow()
{
    std::unique_ptr<short[]> readData = std::make_unique<short[]>(n_samples);
    int i = 0;
    for (auto& item : window) {
        readData[i] = item;
        i++;
    }

    return readData;
}

QVector<double> AsyncDFT::normalizeDFT(double e_maximum, QVector<double> dft)
{
    double u_maximum;

    /*Normalize with the greater maximum*/
    if (this->maximum > e_maximum) {
        u_maximum = this->maximum;
    } else {
        u_maximum = e_maximum;
    }

    for(int i=0; i < dft.size(); i++) {
        dft[i] /= u_maximum;
        dft[i] *= 100;
    }

    return dft;
}

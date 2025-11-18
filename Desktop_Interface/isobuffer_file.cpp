#include "isobuffer_file.h"
#include "math.h"
#include <QDebug>

isoBuffer_file::isoBuffer_file(QWidget *parent, int bufferlen, double sampleRate_Hz) : QWidget(parent)
{
    buffer = (float *) calloc(bufferlen, sizeof(float));
    bufferEnd = bufferlen-1;
    samplesPerSecond = sampleRate_Hz;
}

void isoBuffer_file::writeBuffer_float(float* data, int len)
{
    for (int i=0; i<len;i++){
        buffer[back] = (float) data[i];
        if (back == bufferEnd){
            back = 0;
        }
        else back++;
    }
    return;
}

float *isoBuffer_file::readBuffer(double sampleWindow, int numSamples, bool singleBit, double delayOffset)
{
    //ignore singleBit for now
    double timeBetweenSamples = (double) sampleWindow * samplesPerSecond / ((double) numSamples - 1);
    double accumulatedDelay = 0;
    int delaySamples = (int)((double)delayOffset * samplesPerSecond);

    int front = back - 1 - delaySamples;
    if (front < 0) front = 0;
/*
    qDebug() << "sampleWindow" << sampleWindow;
    qDebug() << "numSamples" << numSamples;
    qDebug() << "delayOffset" << delayOffset;
    qDebug() << "samplesPerSecond" << samplesPerSecond;
    qDebug() << "timeBetweenSamples" << timeBetweenSamples;
    qDebug() << "delaySamples" << delaySamples;
    qDebug() << "back" << back;
    qDebug() << "front" << front;
*/
    int idx;
    if(readData!=NULL) free(readData);
    readData = (float *) calloc(numSamples, sizeof(float));


    if(singleBit){
        return readData; //Garbage.  Not supported.
    }else{
        for (int i=0; i<numSamples;i++){
            if (timeBetweenSamples > (double) front){
                qDebug() << "Wrap!";
                accumulatedDelay -= (double) front;
                front = bufferEnd;
            }

            idx = (int) round(((double) front - accumulatedDelay));
            if (idx < 0){
                qDebug() << "Wrap";
                accumulatedDelay--;
                accumulatedDelay -= (double) front;
                front = bufferEnd;
                idx = (int) round(((double) front - accumulatedDelay));
            }
            readData[i] = buffer[idx];
            accumulatedDelay += timeBetweenSamples;
        }
    }

    return readData;
}

void isoBuffer_file::clearBuffer()
{
    for (int i=0; i<bufferEnd;i++){
        buffer[i] = 0;
    }

    back = 0;
}


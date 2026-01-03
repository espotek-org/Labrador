#ifndef DISABLE_SPECTRUM
#include <Eigen/Dense>  // First so that our #defines won't conflict
#endif

#include "isodriver.h"
#include "isobuffer.h"
#include "isobuffer_file.h"
#include <math.h>
#include "daqloadprompt.h"
#include <iostream>

#ifndef DISABLE_SPECTRUM
#include "asyncdft.h"
#include "spline.h"

#define PI 3.141592653589793  // Predefined value for pi
#define PI_2 2*PI
#define PI_4 4*PI
#define PI_6 6*PI
#define PI_8 8*PI
static constexpr int kSpectrumCounterMax = 4;

#define HORICURSORENABLED ((!spectrum & !freqResp & !eyeDiagram & horiCursorEnabled0) | (spectrum & horiCursorEnabled1) | (freqResp & horiCursorEnabled2) | (eyeDiagram & horiCursorEnabled3))
#define VERTCURSORENABLED ((!spectrum & !freqResp & !eyeDiagram & vertCursorEnabled0) | (spectrum & vertCursorEnabled1) | (freqResp & vertCursorEnabled2) | (eyeDiagram & vertCursorEnabled3))
#else
#define HORICURSORENABLED (horiCursorEnabled0)
#define VERTCURSORENABLED (vertCursorEnabled0)
#endif

isoDriver::isoDriver(QWidget *parent) : QLabel(parent)
{
    this->hide();

#ifndef DISABLE_SPECTRUM
    m_asyncDFT = new AsyncDFT();
    m_windowFactors.fill(1.0, m_asyncDFT->n_samples);
    m_windowFactorsSum = m_asyncDFT->n_samples;

    internalBuffer375_CH1 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/20*21, m_asyncDFT->n_samples, this, 1);
    internalBuffer375_CH2 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/20*21, m_asyncDFT->n_samples, this, 2);
    internalBuffer750 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/10*21, m_asyncDFT->n_samples, this, 1);
#else
    internalBuffer375_CH1 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/20*21, this, 1);
    internalBuffer375_CH2 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/20*21, this, 2);
    internalBuffer750 = new isoBuffer(this, MAX_WINDOW_SIZE*ADC_SPS/10*21, this, 1);
#endif

    isoTemp = (char *) malloc(TIMER_PERIOD*ADC_SPF + 8); //8-byte header contains (unsigned long) length

    v0 = new siprint("V", 0);
    v1 = new siprint("V", 0);
    dv = new siprint("V", 0);
    db0 = new siprint("dB", 0);
    db1 = new siprint("dB", 0);
    ddb = new siprint("dB", 0);
    dbmv0 = new siprint("dBmV", 0);
    dbmv1 = new siprint("dBmV", 0);
    ddbmv = new siprint("dBmV", 0);
    t0 = new siprint("s", 0);
    t1 = new siprint("s", 0);
    dt = new siprint("s", 0);
    f = new siprint("Hz", 0);
    f0 = new siprint("Hz", 0);
    f1 = new siprint("Hz", 0);
    df = new siprint("Hz", 0);

    startTimer();

    slowTimer = new QTimer;
    slowTimer->setTimerType(Qt::PreciseTimer);
    slowTimer->start(MULTIMETER_PERIOD);
    connect(slowTimer, SIGNAL(timeout()), this, SLOT(slowTimerTick()));
}

void isoDriver::setDriver(genericUsbDriver *newDriver){
    driver = newDriver;
    qDebug() << "driver = " << driver;
}

void isoDriver::setAxes(QCustomPlot *newAxes){
    axes = newAxes;
    qDebug() << "axes = " << axes;
}

void isoDriver::timerTick(void){
    //qDebug() << "isoDriver SEZ Tick!";
    if(firstFrame){
        autoGain();
        firstFrame = false;
    }

    isoTemp = driver->isoRead(&length);
    //qDebug() << length << "read in!!";
    total_read += length;

    if(fileModeEnabled){
        qDebug() << "File mode is active.  Abort live refresh";
        return;
    }

    if (length==0){
        //Zero length packet means something's gone wrong.  Probably a disconnect.
        qDebug() << "Zero length iso packet!";
        //driver->killMe();
        return;
    }

    // TODO: Do we need to invalidate state when the device is reconnected?
    bool invalidateTwoWireState = true;
    switch(driver->deviceMode){
        case 0:
            if (deviceMode_prev != 0 && deviceMode_prev != 1 && deviceMode_prev != 2)
                clearBuffers(true, false, false);

            frameActionGeneric(1,0);
            break;
        case 1:
            if (deviceMode_prev != 0 && deviceMode_prev != 1 && deviceMode_prev != 2)
                clearBuffers(true, false, false);

            if (deviceMode_prev != 1)
                clearBuffers(false, true, false);

            internalBuffer375_CH2->m_channel = 1;
            frameActionGeneric(1,2);
            if(serialDecodeEnabled_CH1 && serialType == 0){
                internalBuffer375_CH2->serialManage(baudRate_CH1, parity_CH1, hexDisplay_CH1);
            }
            break;
        case 2:
            if (deviceMode_prev != 0 && deviceMode_prev != 1 && deviceMode_prev != 2)
                clearBuffers(true, false, false);
            if (deviceMode_prev != 2)
                clearBuffers(false, true, false);

            frameActionGeneric(1,1);
            break;
        case 3:
            if (deviceMode_prev != 3 && deviceMode_prev != 4)
                clearBuffers(true, false, false);

            frameActionGeneric(2,0);
            if(serialDecodeEnabled_CH1 && serialType == 0){
                internalBuffer375_CH1->serialManage(baudRate_CH1, parity_CH1, hexDisplay_CH1);
            }
            break;
        case 4:
            if (deviceMode_prev != 3 && deviceMode_prev != 4)
                clearBuffers(true, false, false);
            if (deviceMode_prev != 4)
                clearBuffers(false, true, false);

            internalBuffer375_CH2->m_channel = 2;
            frameActionGeneric(2,2);
            if(serialDecodeEnabled_CH1 && serialType == 0){
                internalBuffer375_CH1->serialManage(baudRate_CH1, parity_CH1, hexDisplay_CH1);
            }
            if(serialDecodeEnabled_CH2 && serialType == 0){
                internalBuffer375_CH2->serialManage(baudRate_CH2, parity_CH2, hexDisplay_CH2);
            }
            if (serialDecodeEnabled_CH1 && serialType == 1)
            {
                if (twoWireStateInvalid)
                    twoWire->reset();
                try
                {
                    twoWire->run();
                }
                catch(...)
                {
                    qDebug() << "Resetting I2C";
                    twoWire->reset();
                }
                invalidateTwoWireState = false;
                twoWireStateInvalid = false;
            }
            break;
        case 5:
            break;
        case 6:
            if (deviceMode_prev != 6)
                clearBuffers(false, false, true);
            frameActionGeneric(-1,0);
            break;
        case 7:
            if (deviceMode_prev != 7)
                clearBuffers(true, false, false);
            multimeterAction();
            break;
        default:
            qFatal("Error in isoDriver::timerTick.  Invalid device mode.");
    }
    if (invalidateTwoWireState)
        twoWireStateInvalid = true;

    deviceMode_prev = driver->deviceMode;
    //free(isoTemp);
}

QVector<double> isoDriver::analogConvert(std::vector<short> &in, int TOP, bool AC, int channel)
{
    double scope_gain = (double)(driver->scopeGain);
    double accumulated = 0;
    double accumulated_square = 0;
    currentVmax = -20;
    currentVmin = 20;

    double ref = (channel == 1 ? ch1_ref : ch2_ref);
    double frontendGain = (channel == 1 ? frontendGain_CH1 : frontendGain_CH2);

    QVector<double> out(in.size());
    for (int i = 0; i < out.size(); ++i) {
        out[i] = (in[i] * (vcc/2)) / (frontendGain*scope_gain*TOP);
        if (driver->deviceMode != 7) out[i] += ref;
        #ifdef INVERT_MM
            if (driver->deviceMode == 7) out[i] *= -1;
        #endif

        accumulated += out[i];
        accumulated_square += out[i] * out[i];
        if (out[i] > currentVmax) currentVmax = out[i];
        if (out[i] < currentVmin) currentVmin = out[i];
    }
    currentVmean = accumulated / out.size();
    currentVRMS = sqrt(accumulated_square / out.size());
    if (AC) {
        //Previous measurments are wrong, edit and redo.
        accumulated = 0;
        accumulated_square = 0;
        currentVmax = -20;
        currentVmin = 20;

        for (int i = 0; i < out.size(); ++i) {
            out[i] -= currentVmean;

            accumulated += out[i];
            accumulated_square += out[i] * out[i];
            if (out[i] > currentVmax) currentVmax = out[i];
            if (out[i] < currentVmin) currentVmin = out[i];
        }
        currentVmean = accumulated / out.size();
        currentVRMS = sqrt(accumulated_square / out.size());
    }
    return out;
}

QVector<double> isoDriver::digitalConvert(std::vector<short> &in)
{
    QVector<double> out(in.size());
    double top = display->topRange - (display->topRange - display->botRange) / 10;
    double bot = display->botRange + (display->topRange - display->botRange) / 10;
    for (int i = 0; i < out.size(); ++i) {
        out[i] = in[i] ? top : bot;
    }
    return out;
}

QVector<double> isoDriver::fileStreamConvert(float *in)
{
    QVector<double> out(GRAPH_SAMPLES);
    for (int i = 0; i < out.size(); ++i) {
        out[i] = in[i];
    }
    return out;
}

#ifndef DISABLE_SPECTRUM
// Evaluate the windowing factor for a given window function, number of samples and at a given index
double isoDriver::windowing_factor(int type, int N, int n)
{
    double factor = 1.0;
    switch (type)
    {
    case 0: // Rectangular window
        factor = 1.0;
        break;
    case 1: // Hann window or raised cosine
        factor = 0.5 - 0.5*std::cos(PI_2*n/(N-1));
        break;
    case 2: // Hamming window
        factor = 0.54 - 0.46*std::cos(PI_2*n/(N-1));
        break;
    case 3: // Blackman window
        factor = 0.42 - 0.5*std::cos(PI_2*n/N) + 0.08*std::cos(PI_4*n/N);
        break;
    case 4: // Flat top window
        factor = 0.21557895 - 0.41663158*std::cos(PI_2*n/N) + 0.277263158*std::cos(PI_4*n/N) - 0.083578947*std::cos(PI_6*n/N) + 0.006947368*std::cos(PI_8*n/N);
        break;
    default:
        factor = 1.0;
    }
    return factor;
}
#endif

void isoDriver::startTimer(){
    /*if (isoTimer!=NULL){
        delete isoTimer;
        isoTimer = NULL;
    }
    isoTimer = new QTimer();
    isoTimer->setTimerType(Qt::PreciseTimer);
    isoTimer->start(TIMER_PERIOD);
    connect(isoTimer, SIGNAL(timeout()), this, SLOT(timerTick()));
    //qFatal("ISO TIMER STARTED");*/
}

void isoDriver::clearBuffers(bool ch3751, bool ch3752, bool ch750){
    if(ch3751)
        internalBuffer375_CH1->clearBuffer();
    if(ch3752)
        internalBuffer375_CH2->clearBuffer();
    if(ch750)
        internalBuffer750->clearBuffer();
}

void isoDriver::setVisible_CH2(bool visible){
    axes->graph(1)->setVisible(visible);
}

void isoDriver::setVoltageRange(QWheelEvent* event)
{
    if (doNotTouchGraph && !fileModeEnabled) return;
#ifndef DISABLE_SPECTRUM
    if (freqResp || spectrum) {
        display->setRespAndSpecRanges(event, axes, this);
        return;
    }
    if (eyeDiagram) return;
#endif

    bool isProperlyPaused = properlyPaused();
    double maxWindowSize = fileModeEnabled ? daq_maxWindowSize : ((double)MAX_WINDOW_SIZE);

    display->setVoltageRange(event, isProperlyPaused, maxWindowSize, axes);

    if (!(event->modifiers() == Qt::ControlModifier))
        if (autoGainEnabled && !isProperlyPaused)
            autoGain();
}

DisplayControl::DisplayControl(double left, double right, double top, double bottom)
{
    window = right-left;
    topRange = top;
    botRange = bottom;
    leftRange = left;
    rightRange = right;
}

#ifndef DISABLE_SPECTRUM
void DisplayControl::setRespAndSpecRanges(QWheelEvent* event, QCustomPlot* axes, isoDriver* driver)
{
    double steps = event->delta() / 120.0;
    double c;
    double pixPct;
    if (!(event->modifiers() == Qt::ControlModifier) && event->orientation() == Qt::Orientation::Vertical) {
        QCPRange range = axes->yAxis->range();
        double upper_range = range.upper;
        double lower_range = range.lower;

        c = (upper_range - lower_range) / 400.0;
        pixPct = 100.0 - (100.0 * (((double)axes->yAxis->pixelToCoord(event->y())-range.lower) / range.size()));
        if (pixPct < 0.0) pixPct = 0.0;
        if (pixPct > 100.0) pixPct = 100.0;
        upper_range -= steps * c * pixPct;
        lower_range += steps * c * (100.0 - pixPct);

        if (upper_range > 90.0) upper_range = 90.0;
        if (lower_range < -90.0) lower_range = -90.0;

        topRange = upper_range;
        botRange = lower_range;
        topRangeUpdated(topRange);
        botRangeUpdated(botRange);
    } else {
        QCPRange range = axes->xAxis->range();
        double upper_range = range.upper;
        double lower_range = range.lower;

        if(axes->xAxis->scaleType()==QCPAxis::stLogarithmic) {
            pixPct = 100.0 * (log((double)axes->xAxis->pixelToCoord(event->x())) - log(range.lower));
            pixPct /= log(range.upper) - log(range.lower);
        } else {
            pixPct = 100.0 * ((double)axes->xAxis->pixelToCoord(event->x()) - range.lower);
            pixPct /= range.size();
        }
        if (pixPct < 0.0) pixPct = 0.0;
        if (pixPct > 100.0) pixPct = 100.0;

        if(axes->xAxis->scaleType()==QCPAxis::stLogarithmic) {
            upper_range *= pow(upper_range/lower_range , (-steps * (100.0-pixPct)/200.));
            lower_range *= pow(upper_range/lower_range , (steps * pixPct)/200.);
            if (lower_range < 1.0) lower_range = 1.0;
        } else {
            c = (upper_range - lower_range) / 200.0;
            upper_range -= steps * c * (100.0 - pixPct);
            lower_range += steps * c * pixPct;
            if (lower_range < 0.0) lower_range = 0.0;
        }

        qDebug() << "WHEEL @ " << pixPct << "%";
        qDebug() << c * pixPct;

        if (upper_range > 750.0e3) upper_range = 750.0e3;
        leftRange = lower_range;
        rightRange = upper_range;

        if(axes->xAxis->scaleType()==QCPAxis::stLogarithmic) {
            driver->retickXAxis();
        }
    }
}
#endif



void DisplayControl::setVoltageRange (QWheelEvent* event, bool isProperlyPaused, double maxWindowSize, QCustomPlot* axes)
{
    double steps = event->delta() / 120.0;
    if (!(event->modifiers() == Qt::ControlModifier) && event->orientation() == Qt::Orientation::Vertical) {
        double c = (topRange - botRange) / 400.0;
        QCPRange range = axes->yAxis->range();

        double pixPct = 100.0 - (100.0 * (((double)axes->yAxis->pixelToCoord(event->y())-range.lower) / range.size()));
        if (pixPct < 0.0) pixPct = 0.0;
        if (pixPct > 100.0) pixPct = 100.0;

        qDebug() << "WHEEL @ " << pixPct << "%";
        qDebug() << range.upper;

        topRange -= steps * c * pixPct;
        botRange += steps * c * (100.0 - pixPct);

        if (topRange > 20.0) topRange = 20.0;
        if (botRange < -20.0) botRange = -20.0;
        topRangeUpdated(topRange);
        botRangeUpdated(botRange);
    } else {
        double c = window / 200.0;
        QCPRange range = axes->xAxis->range();

        double pixPct = 100.0 * ((double)axes->xAxis->pixelToCoord(event->x()) - range.lower);
        pixPct /= isProperlyPaused ? range.size() : window;
        if (pixPct < 0.0) pixPct = 0.0;
        if (pixPct > 100.0) pixPct = 100.0;

        qDebug() << "WHEEL @ " << pixPct << "%";

        if (! isProperlyPaused)
        {
            qDebug() << "TIGGERED";
            qDebug() << "upper = " << range.upper << "lower = " << range.lower;
            qDebug() << "window = " << window;
            qDebug() << c * pixPct;
            qDebug() << c * (100.0 - pixPct) * pixPct / 100.0;
        }

        window -= steps * c * pixPct;
        delay += steps * c * (100.0 - pixPct) * pixPct / 100.0;

        // NOTE: delayUpdated and timeWindowUpdated are called more than once beyond here,
        // maybe they should only be called once at the end?

        delayUpdated(delay);
        timeWindowUpdated(window);

        qDebug() << window << delay;

        if (window > maxWindowSize)
        {
            window = maxWindowSize;
            timeWindowUpdated(window);
        }
        if ((window + delay) > maxWindowSize)
        {
            delay = maxWindowSize - window;
            delayUpdated(delay);
        }
        if (delay < 0)
        {
            delay = 0;
            delayUpdated(delay);
        }
    }
}

bool isoDriver::properlyPaused(){
    if(paused_CH1 & paused_CH2){
        qDebug() << "Properly paused";
        return true;
    }
    if ((driver->deviceMode == 0) || (driver->deviceMode == 3) || (driver->deviceMode == 6)){
        if(paused_CH1) qDebug() << "Properly paused"; else qDebug() << "Not properly paused";
        return paused_CH1;
    }
    if(paused_multimeter){
        qDebug() << "Properly paused";
        return true;
    }
    qDebug() << "Not properly paused";
    return false;
}

void isoDriver::pauseEnable_CH1(bool enabled){
    paused_CH1 = enabled;

    if(!properlyPaused()) {
        display->delay = 0;
        delayUpdated(display->delay);
        if (autoGainEnabled) autoGain();
    }

    if(!enabled) clearBuffers(1,0,1);
    qDebug() << "pauseEnable_CH1" << enabled;
}


void isoDriver::pauseEnable_CH2(bool enabled){
    paused_CH2 = enabled;

    if(!properlyPaused()){
        display->delay = 0;
        delayUpdated(display->delay);
        if (autoGainEnabled) autoGain();
    }

    if(!enabled) clearBuffers(0,1,0);
}

void isoDriver::pauseEnable_multimeter(bool enabled){
    paused_multimeter = enabled;

    if(!properlyPaused()) {
        display->delay = 0;
        delayUpdated(display->delay);
    }

    if(!enabled) clearBuffers(1,0,0);
    qDebug() << "pauseEnable_multimeter" << enabled;
}


void isoDriver::autoGain(){
    double maxgain = vcc / (2 * ((double)display->topRange - vref) * R4/(R3+R4));
    double mingain = vcc / (2 * ((double)display->botRange - vref) * R4/(R3+R4));
    maxgain = fmin(fabs(mingain) * 0.98, fabs(maxgain) * 0.98);

    double snap[8] = {64, 32, 16, 8, 4, 2, 1, 0.5};

    for (int i=0;i<8;i++){
        if (maxgain>snap[i]){
            setGain(snap[i]);
            return;
        }
    }
}

void isoDriver::gainBuffers(double multiplier){
    multi = multiplier;
    QTimer::singleShot(TIMER_PERIOD*4, this, SLOT(gainTick()));
}

void isoDriver::gainTick(void){
#ifdef PLATFORM_ANDROID
#warning: "gainTick does nothing on Android!!"
#else
    qDebug() << "Multiplying by " << multi;
    if (driver->deviceMode <5) internalBuffer375_CH1->gainBuffer(log2(multi));
    if ((driver->deviceMode == 1) | (driver->deviceMode == 2) | (driver->deviceMode == 4)) internalBuffer375_CH2->gainBuffer(log2(multi));
    if ((driver->deviceMode == 6) | (driver->deviceMode == 7)) internalBuffer750->gainBuffer(log2(multi));
#endif
}

void isoDriver::setAutoGain(bool enabled){
    autoGainEnabled = enabled;
    if(enabled){
        autoGain();
    }
}

void isoDriver::graphMousePress(QMouseEvent *event){
    qDebug() << event->button();
    if (HORICURSORENABLED && (event->button() == Qt::LeftButton)){
        placingHoriAxes = true;
        display->y0 = axes->yAxis->pixelToCoord(event->y());
#ifndef PLATFORM_ANDROID
    }else if(VERTCURSORENABLED && (event->button() == Qt::RightButton)){
#else
    }if(VERTCURSORENABLED){
#endif
        placingVertAxes = true;
        display->x0 = axes->xAxis->pixelToCoord(event->x());
    }
    qDebug() << "x0 =" << display->x0 << "x1 =" << display->x1 << "y0 =" << display->y0 << "y1 =" << display->y1;
}

void isoDriver::graphMouseRelease(QMouseEvent *event){
    if(HORICURSORENABLED && placingHoriAxes && (event->button() == Qt::LeftButton)){
        placingHoriAxes = false;
#ifndef PLATFORM_ANDROID
    } else if (VERTCURSORENABLED && placingVertAxes && (event->button() == Qt::RightButton)){
#else
    } if (VERTCURSORENABLED && placingVertAxes){
#endif
        placingVertAxes = false;
    }
    qDebug() << "x0 =" << display->x0 << "x1 =" << display->x1 << "y0 =" << display->y0 << "y1 =" << display->y1;
}

void isoDriver::graphMouseMove(QMouseEvent *event){
    if(HORICURSORENABLED && placingHoriAxes){
        display->y1 = axes->yAxis->pixelToCoord(event->y());
#ifndef PLATFORM_ANDROID
    } else if(VERTCURSORENABLED && placingVertAxes){
#else
    } if(VERTCURSORENABLED && placingVertAxes){
#endif
        display->x1 = axes->xAxis->pixelToCoord(event->x());
    }
}

void isoDriver::cursorEnableHori(bool enabled){
#ifndef DISABLE_SPECTRUM
    if(spectrum)
        horiCursorEnabled1 = enabled;
    else if(freqResp)
        horiCursorEnabled2 = enabled;
    else if(eyeDiagram)
        horiCursorEnabled3 = enabled;
    else
#endif
        horiCursorEnabled0 = enabled;
    axes->graph(4)->setVisible(enabled);
    axes->graph(5)->setVisible(enabled);
}

void isoDriver::cursorEnableVert(bool enabled){
#ifndef DISABLE_SPECTRUM
    if(spectrum)
        vertCursorEnabled1 = enabled;
    else if(freqResp)
        vertCursorEnabled2 = enabled;
    else if(eyeDiagram)
        vertCursorEnabled3 = enabled;
    else
#endif
        vertCursorEnabled0 = enabled;
    axes->graph(2)->setVisible(enabled);
    axes->graph(3)->setVisible(enabled);
}

void isoDriver::updateCursors(){
    if(!(VERTCURSORENABLED || HORICURSORENABLED)){
#if QCP_VER == 1
        cursorLabel->setVisible(0);
#endif
        return;
    }

    QVector<double> vert0x(2), vert1x(2), hori0x(2), hori1x(2), vert0y(2), vert1y(2), hori0y(2), hori1y(2);

    vert0x[0] = display->x0;
    vert0x[1] = display->x0;
    vert0y[0] = display->botRange;
    vert0y[1] = display->topRange;

    vert1x[0] = display->x1;
    vert1x[1] = display->x1;
    vert1y[0] = display->botRange;
    vert1y[1] = display->topRange;

#ifndef DISABLE_SPECTRUM
    hori0x[0] = (spectrum || freqResp || eyeDiagram) ? -display->window : -display->window - display->delay;
    hori0x[1] = (spectrum || freqResp || eyeDiagram) ?  display->window : -display->delay;
#else
    hori0x[0] = -display->window - display->delay;
    hori0x[1] = -display->delay;
#endif
    hori0y[0] = display->y0;
    hori0y[1] = display->y0;

#ifndef DISABLE_SPECTRUM
    hori1x[0] = (spectrum || freqResp || eyeDiagram) ? -display->window : -display->window - display->delay;
    hori1x[1] = (spectrum || freqResp || eyeDiagram) ?  display->window : -display->delay;
#else
    hori1x[0] = -display->window - display->delay;
    hori1x[1] = -display->delay;
#endif
    hori1y[0] = display->y1;
    hori1y[1] = display->y1;

    if(VERTCURSORENABLED){
        axes->graph(2)->setData(vert0x, vert0y);
        axes->graph(3)->setData(vert1x, vert1y);
    }
    if(HORICURSORENABLED){
        axes->graph(4)->setData(hori0x, hori0y);
        axes->graph(5)->setData(hori1x, hori1y);
    }
#if QCP_VER == 1
    cursorLabel->setVisible(cursorStatsEnabled);
#endif
    if (!cursorStatsEnabled) return;

    QString temp_hori;
    QString temp_vert;
#ifndef DISABLE_SPECTRUM
    if(spectrum)
    {
        dbmv0->value = display->y0;
        dbmv1->value = display->y1;
        ddbmv->value = std::fabs(display->y1 - display->y0);
        f0->value = display->x0;
        f1->value = display->x1;
        df->value = std::fabs(display->x1 - display->x0);
        temp_hori = QString::asprintf("P0:%11s\nP1:%11s\nΔP:%11s", dbmv0->printVal(), dbmv1->printVal(), ddbmv->printVal());
        temp_vert = QString::asprintf("f0:%11s\nf1:%11s\nΔf:%11s", f0->printVal(), f1->printVal(), df->printVal());
    }
    else if(freqResp)
    {
        db0->value = display->y0;
        db1->value = display->y1;
        ddb->value = std::fabs(display->y1 - display->y0);
        f0->value = display->x0;
        f1->value = display->x1;
        df->value = std::fabs(display->x1 - display->x0);
        temp_hori = QString::asprintf("P0:%9s\nP1:%9s\nΔP:%9s", db0->printVal(), db1->printVal(), ddb->printVal());
        temp_vert = QString::asprintf("f0:%9s\nf1:%9s\nΔf:%9s", f0->printVal(), f1->printVal(), df->printVal());
    }
    else
#endif
    {
        v0->value = display->y0;
        v1->value = display->y1;
        dv->value = std::fabs(display->y1 - display->y0);
        t0->value = display->x0;
        t1->value = display->x1;
        dt->value = std::fabs(display->x1 - display->x0);
        f->value = 1.0 / dt->value;
        temp_hori = QString::asprintf("V0:%8s\nV1:%8s\nΔV:%8s", v0->printVal(), v1->printVal(), dv->printVal());
        temp_vert = QString::asprintf("t0:%8s\nt1:%8s\nΔt:%8s\nf:%8s", t0->printVal(), t1->printVal(), dt->printVal(), f->printVal());
    }

    QString cursorStatsString;
    if (HORICURSORENABLED) cursorStatsString.append(temp_hori);
    if (HORICURSORENABLED && VERTCURSORENABLED) cursorStatsString.append("\n");
    if (VERTCURSORENABLED) cursorStatsString.append(temp_vert);
#if QCP_VER == 1
    cursorLabel->setText(cursorStatsString);
#endif
}

short isoDriver::reverseFrontEnd(double voltage){
    //qFatal("reverseFrontEnd driver mode 7");
    #ifdef INVERT_MM
        if(driver->deviceMode == 7) voltage *= -1;
    #endif


    double vn = vcc * (R2/(R1+R2));
    double vx = vn + (voltage - vn) * (R4 / (R3+R4));
    double TOP = (driver->deviceMode == 7) ? 2048 : 128;

    if (driver->deviceMode == 7){
        qDebug() << "SEEEKING";
        qDebug() <<  ((vx - vn)/vref * (double)driver->scopeGain * (double)TOP + (double)0.5);
        qDebug() << "SEEEKING";
        return ((vx - vn)/vref * (double)driver->scopeGain * (double)TOP + (double)0.5);
    }

    //qDebug() << "seeking" << voltage << "V";


    return ((vx - vn)/vref * (double)driver->scopeGain * (double)TOP + (double)0.5);
}

void isoDriver::setTriggerEnabled(bool enabled)
{
    triggerEnabled = enabled;
    triggerStateChanged();
}

void isoDriver::setTriggerLevel(double level)
{
    internalBuffer375_CH1->setTriggerLevel(level, (driver->deviceMode == 7 ? 2048 : 128), AC_CH1);
    internalBuffer375_CH2->setTriggerLevel(level, 128, AC_CH2);
    internalBuffer750->setTriggerLevel(level, 128, AC_CH1);
    triggerStateChanged();
}

void isoDriver::setSingleShotEnabled(bool enabled)
{
    singleShotEnabled = enabled;
    triggerStateChanged();
}

void isoDriver::setTriggerMode(int newMode)
{
    triggerMode = newMode;
    triggerStateChanged();
}

//0 for off, 1 for ana, 2 for dig, -1 for ana750, -2 for file
void isoDriver::frameActionGeneric(char CH1_mode, char CH2_mode)
{
#ifndef DISABLE_SPECTRUM
    internalBuffer375_CH1->enableFreqResp(freqResp, freqValue_CH1->value());
    internalBuffer375_CH2->enableFreqResp(freqResp, freqValue_CH1->value());
#endif

    //qDebug() << "made it to frameActionGeneric";
    if(!paused_CH1 && CH1_mode == - 1){
        for (unsigned int i=0;i<(length/ADC_SPF);i++){
            internalBuffer750->writeBuffer_char(&isoTemp[ADC_SPF*i], VALID_DATA_PER_750);
        }
    }

    if(!paused_CH1 && CH1_mode > 0){
        for (unsigned int i=0;i<(length/ADC_SPF);i++){
            internalBuffer375_CH1->writeBuffer_char(&isoTemp[ADC_SPF*i], VALID_DATA_PER_375);
        }
    }

    if(!paused_CH2 && CH2_mode > 0){
        for (unsigned int i=0;i<(length/ADC_SPF);i++){
            internalBuffer375_CH2->writeBuffer_char(&isoTemp[ADC_SPF*i+ADC_SPF/2], VALID_DATA_PER_375);  //+375 to get the second half of the packet
        }
    }

    if(!paused_CH1)
    {
        int offset = -2; //No trigger!

        int backLength = length/750;
        backLength *= (CH1_mode == -1) ? VALID_DATA_PER_750 : VALID_DATA_PER_375;

        if(offset>0){
            int temp_offset = offset % 750;
            offset /= 750;
            offset *= (CH1_mode == -1) ? VALID_DATA_PER_750 : VALID_DATA_PER_375;
            offset += temp_offset;
        }

        //qDebug() << "Now offset = " << offset;
    }

    auto internalBuffer_CH1 = (CH1_mode == -1) ? internalBuffer750 : internalBuffer375_CH1;
    auto internalBuffer_CH2 = internalBuffer375_CH2;

    double triggerDelay = 0;
    if (triggerEnabled)
    {
        triggerDelay = (triggerMode < 2) ? internalBuffer_CH1->getDelayedTriggerPoint(display->window) - display->window
                                         : internalBuffer_CH2->getDelayedTriggerPoint(display->window) - display->window;

        if (triggerDelay < 0)
            triggerDelay = 0;
    }

    if(singleShotEnabled && (triggerDelay != 0))
        singleShotTriggered(1);

    std::vector<short> readData_CH1;
    std::vector<short> readData_CH2;
    float *readDataFile;

#ifndef DISABLE_SPECTRUM
    if (spectrum || eyeDiagram) {
        // The spectrum and eyeDiagram are computationally expensive to calculate, so we don't want to do it on every frame
        m_spectrumCounter = (m_spectrumCounter + 1) % kSpectrumCounterMax;
        if (m_spectrumCounter != 0)
            return;

        readData_CH1 = internalBuffer_CH1->readWindow();
        readData_CH2 = internalBuffer_CH2->readWindow();
    } else if (freqResp) {
        double freqResp_window = 1/freqValue_CH1->value();
        readData_CH1 = internalBuffer_CH1->readBuffer(freqResp_window, internalBuffer_CH1->freqResp_samples, CH1_mode == 2, triggerDelay);
        readData_CH2 = internalBuffer_CH2->readBuffer(freqResp_window, internalBuffer_CH2->freqResp_samples, CH2_mode == 2, triggerDelay);
    } else
#endif
    {
        if (CH1_mode == -2)
            readDataFile = internalBufferFile->readBuffer(display->window, GRAPH_SAMPLES, false, display->delay);
        else if (CH1_mode)
            readData_CH1 = internalBuffer_CH1->readBuffer(display->window, GRAPH_SAMPLES, CH1_mode == 2, display->delay + triggerDelay);
        if (CH2_mode)
            readData_CH2 = internalBuffer_CH2->readBuffer(display->window, GRAPH_SAMPLES, CH2_mode == 2, display->delay + triggerDelay);
    }

    QVector<double> CH1, CH2;
#ifndef DISABLE_SPECTRUM
    double CH1_avg = 0;
    double samplesPerSymbol = 0;
    int numSymbols = 0;
#endif

    if (CH1_mode == -1 || CH1_mode == 1) {
        CH1 = analogConvert(readData_CH1, 128, AC_CH1, 1);
#ifndef DISABLE_SPECTRUM
        if (spectrum) {
            for (int i = 0; i < CH1.size(); ++i) {
                CH1[i] /= m_attenuation_CH1;
                CH1[i] += m_offset_CH1;
                CH1[i] *= m_windowFactors[i];
            }
        } else
#endif
        {
            for (int i = 0; i < CH1.size(); ++i) {
                CH1[i] /= m_attenuation_CH1;
                CH1[i] += m_offset_CH1;
            }

#ifndef DISABLE_SPECTRUM
            if(eyeDiagram)
            {
                // Analog to digital conversion
                double HYSTERESIS = 0.5;
                CH1_avg = std::accumulate(CH1.begin(), CH1.end(), 0.0) / CH1.size();
                QVector<int> CH1_bin(CH1.size());
                CH1_bin[0] = 0;
                for (int i = 1; i < CH1.size(); ++i)
                {
                    if(CH1_bin[i-1] == 0 && CH1[i] > CH1_avg+HYSTERESIS)
                        CH1_bin[i] = 1;
                    else if(CH1_bin[i-1] == 1 && CH1[i] < CH1_avg-HYSTERESIS)
                        CH1_bin[i] = 0;
                    else
                        CH1_bin[i] = CH1_bin[i-1];
                }

                // Calculate edges
                QVector<double> CH1_edge(CH1_bin.size());
                for (int i = 1; i < CH1_bin.size(); ++i)
                    CH1_edge[i] = std::abs(CH1_bin[i] - CH1_bin[i-1]);

                // Find the index of the peak frequency
                // Calculate power (frequency) spectrum
                setWindowingType(1);	// Hann window
                QVector<double> yf = m_asyncDFT->getPowerSpectrum_dBmV(CH1_edge, m_windowFactorsSum);
                // Create an index vector excluding the DC component
                QVector<int> indices(yf.size()-10);
                std::iota(indices.begin(), indices.end(), 10);
                // Sort the index vector based on the values in the power spectrum
                std::sort(indices.begin(), indices.end(),
                    [&yf](int i, int j) {
                        return yf[i] < yf[j];
                    });
                // Calculate the mininum from the biggest five indices
                QVector<int> biggestFiveIndices = indices.mid(indices.size() - 5, 5);
                int peak_idx = *std::min_element(biggestFiveIndices.begin(), biggestFiveIndices.end());

                // Create a cubic spline interpolation function
                std::vector<double> xf_partial(11);
                std::vector<double> yf_partial(11);
                for (int i = 0; i < 11; ++i) {
                    xf_partial[i] = peak_idx-5+i;
                    yf_partial[i] = std::abs(yf[xf_partial[i]]);
                }
                tk::spline cs(xf_partial, yf_partial);

                // Generate a fine grid of x values to evaluate the cubic spline
                QVector<double> x_fine(201);
                QVector<double> y_fine(201);
                double step = (xf_partial[10] - xf_partial[0]) / 200;
                for (int i = 0; i < 201; ++i) {
                    x_fine[i] = xf_partial[0] + i * step;
                    y_fine[i] = cs(x_fine[i]);
                }

                // calculate normalized fundamental frequency
                double *max_it = std::max_element(y_fine.begin(), y_fine.end());
                peak_idx = std::distance(y_fine.begin(), max_it);
                double fundamental_frequency_normalized = x_fine[peak_idx]/CH1.size();

                // Calculate samples per symbol and number of symbols
                samplesPerSymbol = 1 / (2*fundamental_frequency_normalized);
                numSymbols = std::round(CH1.size()/samplesPerSymbol);
                samplesPerSymbol = std::round(samplesPerSymbol);
            }
#endif
        }

        xmin = (currentVmin < xmin) ? currentVmin : xmin;
        xmax = (currentVmax > xmax) ? currentVmax : xmax;
        broadcastStats(0);
    } else if (CH1_mode == 2) {
        CH1 = digitalConvert(readData_CH1);
    } else if (CH1_mode == -2) {
        CH1 = fileStreamConvert(readDataFile);
    }

    if (CH2_mode == 1) {
        CH2 = analogConvert(readData_CH2, 128, AC_CH2, 2);
#ifndef DISABLE_SPECTRUM
        if (spectrum) {
            for (int i = 0; i < CH2.size(); ++i) {
                CH2[i] /= m_attenuation_CH2;
                CH2[i] += m_offset_CH2;
                CH2[i] *= m_windowFactors[i];
            }
        } else
#endif
        {
            for (int i = 0; i < CH2.size(); ++i) {
                CH2[i] /= m_attenuation_CH2;
                CH2[i] += m_offset_CH2;
            }
        }

        ymin = (currentVmin < ymin) ? currentVmin : ymin;
        ymax = (currentVmax > ymax) ? currentVmax : ymax;
        broadcastStats(1);
    } else if (CH2_mode == 2) {
        CH2 = digitalConvert(readData_CH2);
    }


    QVector<double> x(GRAPH_SAMPLES);
    for (int i = 0; i < x.size(); ++i) {
        x[i] = -(display->window*i)/((double)(GRAPH_SAMPLES-1)) - display->delay;
        if (x[i]>0) {
            CH1[i] = 0;
            CH2[i] = 0;
        }
    }

    updateCursors();

    if (XYmode) {
        QCPCurve* curve = reinterpret_cast<QCPCurve*>(axes->plottable(0));
        curve->setData(CH1, CH2);
        axes->xAxis->setLabel("CH1 (V)");
        axes->yAxis->setLabel("CH2 (V)");
        axes->xAxis->setRange(xmin, xmax);
        axes->yAxis->setRange(ymin, ymax);

#ifndef DISABLE_SPECTRUM
    } else if (spectrum) {
        /*Getting array of frequencies for display purposes*/
        auto f = m_asyncDFT->getFrequencyWindow(internalBuffer_CH1->m_samplesPerSecond);

        /*Creating DFT amplitudes*/
        auto amplitude = m_asyncDFT->getPowerSpectrum_dBmV(CH1, m_windowFactorsSum);
        axes->graph(0)->setData(f, amplitude);
        if (CH2_mode) {
            auto amplitude = m_asyncDFT->getPowerSpectrum_dBmV(CH2, m_windowFactorsSum);
            axes->graph(1)->setData(f, amplitude);
        }

        axes->xAxis->setLabel("Frequency (Hz)");
        axes->yAxis->setLabel("Relative Power (dBmV)");
        axes->xAxis->setRange(display->leftRange, display->rightRange);
        axes->yAxis->setRange(display->botRange, display->topRange);

    } else if (freqResp) {
        if (!paused_CH1) {
            // Using least squares, fit a sinusoid to measured samples
            int nof_elements = CH1.size();
            double delta = 2 * PI / (nof_elements - 1);
            double amp1, amp2, gain, gain_avg_db, phase1, phase2, phase_diff, phase_avg, norm_rms1, norm_rms2;
            static double gain_sum = 0, phase_sum = 0;
            static int freqResp_cnt = 0, err_cnt = 0;
            Eigen::MatrixXf A(nof_elements, 3);
            Eigen::VectorXf b1(nof_elements), b2(nof_elements), x1(3), x2(3), r1(nof_elements), r2(nof_elements);
            for (int i = 0; i < nof_elements; i++) {
                A(i, 0) = 1;
                A(i, 1) = std::sin(i*delta);
                A(i, 2) = std::cos(i*delta);
                b1(i) = CH1[i];
                b2(i) = CH2[i];
            }

            // Solve the least squares solution Ax=b (using QR decomposition)
            x1 = A.colPivHouseholderQr().solve(b1);
            x2 = A.colPivHouseholderQr().solve(b2);

            // Calculate amplitude and phase
            amp1 = hypot(x1(1), x1(2));
            amp2 = hypot(x2(1), x2(2));
            phase1 = atan2(-x1(2), x1(1)) * 180.0 / PI;
            phase2 = atan2(-x2(2), x2(1)) * 180.0 / PI;

            // Calculate normalized fitting risidual
            r1 = A * x1 - b1;
            r2 = A * x2 - b2;
            norm_rms1 = sqrt((r1/amp1).cwiseAbs2().mean());
            norm_rms2 = sqrt((r2/amp2).cwiseAbs2().mean());
            // std::cout << "rms1 = " << norm_rms1 << " rms2 = " << norm_rms2 << "\n";

            // If there is too much error in the least square fitting, discard current trace
            if (norm_rms1 > 0.1 || norm_rms2 > 2) {
                err_cnt++;
            } else {
                // Calculate gain, and phase difference
                gain = amp2 / amp1;
                phase_diff = phase2 - phase1;
                phase_diff = phase_diff > 180 ? phase_diff - 360 : (phase_diff < -180 ? phase_diff + 360 : phase_diff);

                // Calculate average gain (dB) and average phase
                if (freqResp_cnt < 10) {
                    gain_sum += gain;
                    phase_sum += phase_diff;
                } else {
                    gain_avg_db = 20 * log10(gain_sum/freqResp_cnt);
                    phase_avg = phase_sum/freqResp_cnt;
                    // std::cout << "gain_dB = " << gain_avg_db << " phase = " << phase_avg << "\n";

                    // Search first occurrence
                    int index = m_freqRespFreq.indexOf(freqValue_CH1->value());
                    if (index != -1) {
                        // Update if record exists
                        m_freqRespGain[index] = gain_avg_db;
                        m_freqRespPhase[index] = phase_avg;
                    } else {
                        // Append if record does not exist
                        m_freqRespFreq.append(freqValue_CH1->value());
                        m_freqRespGain.append(gain_avg_db);
                        m_freqRespPhase.append(phase_avg);
                    }
                }
                freqResp_cnt ++;
            }

            // Prepare for next cycle
            if (freqResp_cnt > 10 || err_cnt > 10) {
                // Reset frequency response vectors, when a user updates min/max/step parameters
                if (m_freqRespFlag) {
                    m_freqRespFreq.clear();
                    m_freqRespGain.clear();
                    m_freqRespPhase.clear();
                    m_freqRespFlag = false;
                }

                // Select new frequency
                double freqValue = freqValue_CH1->value() + m_freqRespStep;
                if (freqValue > m_freqRespMax || freqValue < m_freqRespMin || m_freqRespFreq.size() == 0)
                    freqValue = m_freqRespMin;
                freqValue_CH1->setValue(freqValue);

                // Reset iterators
                gain_sum = 0;
                phase_sum = 0;
                freqResp_cnt = 0;
                err_cnt = 0;
            }
        }

        axes->xAxis->setLabel("Frequency (Hz)");
        if (m_freqRespType == 0) {
            // Plot gain response
            axes->graph(0)->setData(m_freqRespFreq, m_freqRespGain);
            axes->yAxis->setLabel("Gain (dB)");
        } else {
            // Plot phase response
            axes->graph(0)->setData(m_freqRespFreq, m_freqRespPhase);
            axes->yAxis->setLabel("Phase (degree)");
        }
        axes->xAxis->setRange(display->leftRange, display->rightRange);
        axes->yAxis->setRange(display->botRange, display->topRange);
        axes->graph(1)->clearData();
    } else if (eyeDiagram){

        int numOutputSamples = numSymbols*samplesPerSymbol;

        // Resample at integer samplesPerSymbol
        QVector<double> CH1_resampled(numOutputSamples);
        double inputSize = static_cast<double>(CH1.size());
        double outputSize = static_cast<double>(numOutputSamples);
        for (int i = 0; i < numOutputSamples; ++i) {
            double idx = (i + 0.5) * (inputSize-1) / outputSize;
            int lb_idx = static_cast<size_t>(idx);
            int ub_idx = lb_idx + 1;
            double fraction = idx - lb_idx;

            // Interpolate between data points
            CH1_resampled[i] = CH1[lb_idx] * (1.0 - fraction) + CH1[ub_idx] * fraction;
        }

        // Analog to digital conversion
        QVector<double> CH1_bin(CH1_resampled.size());
        for (int i = 0; i < CH1_resampled.size(); ++i)
            CH1_bin[i] = (CH1_resampled[i] > CH1_avg) ? 1 : 0;

        // Calculate edges
        QVector<double> CH1_edge(CH1_bin.size());
        for (int i = 1; i < CH1_bin.size(); ++i)
            CH1_edge[i] = std::abs(CH1_bin[i] - CH1_bin[i-1]);

        // Clock Recovery using digital PLL
        uint16_t NCO_BIAS  = (1l << 16) / samplesPerSymbol;
        uint16_t PHASE_TARGET = 300 * (1l << 16) / 360;

        uint16_t nco_phase = 0;
        int32_t nco_word = 0;

        constexpr int16_t Kp = 6; // bit shift division
        constexpr int16_t Ki = 9; // bit shift division
        int32_t integrator = 0;

        int32_t error = 0;
        QVector<int> error_abs;
        int32_t moving_avg_size = 5 * samplesPerSymbol;
        QVector<double> error_avg_list;

        // digital PLL Clock Recovery Loop
        for (int i = 1; i < CH1_edge.size(); ++i)
        {
            // Edge detection
            if (CH1_edge[i])
            {
                error = (int32_t)PHASE_TARGET - nco_phase;
                integrator += error;
                nco_word = (error >> Kp) + (integrator >> Ki);
            }
            error_abs.append(std::abs(error));
            nco_phase += (nco_word + NCO_BIAS);

            // Store the average phase error
            if(i > moving_avg_size)
            {
                double error_avg = std::accumulate(error_abs.end()-moving_avg_size, error_abs.end(), 0.0) / moving_avg_size;
                error_avg_list.append(error_avg);
            }
        }

        // After PLL is locked (small average error), it can loose lock due to noise or other disturbances.
        // Hence, we need to find a segment where the average error is below a given threshold
        // for a given number of samples.
        int N = 10000;
        double error_threshold = 100.0;
        int counter = 0;
        for (int i = 0; i < error_avg_list.size(); ++i)
        {
            if (error_avg_list[i] < error_threshold) {
                counter += 1;
                if (counter == N)
                {
                    double *max_it = std::max_element(CH1_edge.begin()+i-N, CH1_edge.end());
                    int idx = std::distance(CH1_edge.begin(), max_it) + int(samplesPerSymbol/2);
                    CH1_resampled = CH1_resampled.mid(idx, N);
                    break;
                }
            } else {
                counter = 0;  // Reset counter if value is not less than threshold
            }
        }

        // Plot the overlaid segments
        int nof_symbols = std::min(int(CH1_resampled.size()/samplesPerSymbol), 96);
        for (int i = 0; i <= nof_symbols-2; ++i) {
           QVector<double> x(2 * samplesPerSymbol);
           QVector<double> y(2 * samplesPerSymbol);
           for (int j = 0; j < 2 * samplesPerSymbol; ++j) {
               x[j] = (j - samplesPerSymbol)/internalBuffer_CH1->m_samplesPerSecond;
               y[j] = CH1_resampled[i*samplesPerSymbol + j];
            }
            axes->graph(6+i)->setData(x, y);
        }

        // plot the eye diagram
        axes->xAxis->setLabel("Time (samples)");
        axes->yAxis->setLabel("Voltage (V)");
        axes->xAxis->setRange(-samplesPerSymbol/internalBuffer_CH1->m_samplesPerSecond, samplesPerSymbol/internalBuffer_CH1->m_samplesPerSecond);
        axes->yAxis->setRange(std::round(*std::min_element(CH1_resampled.begin(), CH1_resampled.end()))-1, std::round(*std::max_element(CH1_resampled.begin(), CH1_resampled.end()))+1);

        axes->graph(0)->clearData();
        axes->graph(1)->clearData();
#endif

    } else {
        axes->graph(0)->setData(x,CH1);
        if (CH2_mode) axes->graph(1)->setData(x,CH2);
        axes->xAxis->setLabel("Time (sec)");
        axes->yAxis->setLabel("Voltage (V)");
        axes->xAxis->setRange(-display->window - display->delay, -display->delay);
        axes->yAxis->setRange(display->topRange, display->botRange);

        for (int i = 0; i <= 94; ++i)
            axes->graph(6+i)->clearData();
    }

    if(snapshotEnabled_CH1){
#ifndef DISABLE_SPECTRUM
        if (!spectrum && !freqResp)
#endif
        {
            snapshotFile_CH1->open(QIODevice::WriteOnly);
            snapshotFile_CH1->write("t, v\n");

            char tempchar[32];
            for(int i=0; i<GRAPH_SAMPLES; i++){
                snprintf(tempchar, sizeof tempchar, "%f, %f\n", x.at(i), CH1.at(i));
                snapshotFile_CH1->write(tempchar);
            }
            snapshotFile_CH1->close();
        }
        snapshotEnabled_CH1 = false;
        delete(snapshotFile_CH1);
    }

    if(snapshotEnabled_CH2){
#ifndef DISABLE_SPECTRUM
        if (!spectrum && !freqResp && CH2_mode)
#else
        if (CH2_mode)
#endif
        {
            snapshotFile_CH2->open(QIODevice::WriteOnly);
            snapshotFile_CH2->write("t, v\n");

            char tempchar[32];
            for(int i=0; i<GRAPH_SAMPLES; i++){
                snprintf(tempchar, sizeof tempchar, "%f, %f\n", x.at(i), CH2.at(i));
                snapshotFile_CH2->write(tempchar);
            }
            snapshotFile_CH2->close();
        }
        snapshotEnabled_CH2 = false;
        delete(snapshotFile_CH2);
    }

    axes->replot();
}

void isoDriver::multimeterAction(){
    isoTemp_short = (short *)isoTemp;
    if(!paused_multimeter){
        for (unsigned int i=0;i<(length/ADC_SPF);i++){
            internalBuffer375_CH1->writeBuffer_short(&isoTemp_short[ADC_SPF/2*i], ADC_SPF/2-1);  //Offset because the first 8 bytes of the array contain the length (no samples!!)!
        }
    }

    double triggerDelay = 0;
    if (triggerEnabled)
    {
        triggerDelay = internalBuffer375_CH1->getDelayedTriggerPoint(display->window) - display->window;

        if (triggerDelay < 0)
            triggerDelay = 0;
    }

    if(singleShotEnabled && (triggerDelay != 0))
        singleShotTriggered(1);

    auto readData_CH1 = internalBuffer375_CH1->readBuffer(display->window, GRAPH_SAMPLES, false, display->delay + triggerDelay);
    auto CH1 = analogConvert(readData_CH1, 2048, 0, 1);  //No AC coupling!

    QVector<double> x(CH1.size());
    for (int i = 0; i < x.size(); ++i) {
        x[i] = -(display->window*i)/((double)(GRAPH_SAMPLES-1)) - display->delay;
        if (x[i]>0) {
            CH1[i] = 0;
        }
    }
    axes->graph(0)->setData(x,CH1);

    updateCursors();

    axes->xAxis->setRange(-display->window - display->delay, -display->delay);
    axes->yAxis->setRange(display->topRange, display->botRange);

    axes->replot();
    multimeterStats();
}

void isoDriver::setAC_CH1(bool enabled){
    AC_CH1 = enabled;
}

void isoDriver::setAC_CH2(bool enabled){
    AC_CH2 = enabled;
}

void isoDriver::setMultimeterType(int type){
    multimeterType = (multimeterType_enum) type;
    switch (type){

    case R:
        multimeterREnabled(multimeterRsource);
        break;
    case C:
        multimeterREnabled(254);
        break;
    default:
        multimeterREnabled(255);
    }

    qDebug() << "multimeterType = " << multimeterType;
}

void isoDriver::setSeriesResistance(double resistance){
    seriesResistance = resistance;
    qDebug() << "seriesResistance = " << seriesResistance;
}

void isoDriver::multimeterStats(){
    //qDebug() << "Entering isoDriver::multimeterStats()";
    if (!multimeterShow) return;

    QTimer::singleShot(MULTIMETER_PERIOD, this, SLOT(enableMM()));

    multimeterShow = false;
    bool mvMax, mvMin, mvMean, mvRMS, maMax, maMin, maMean, maRMS, kOhms, uFarads;  //We'll let the compiler work out this one.

    if(autoMultimeterV){
        mvMax = abs(currentVmax) < 1.;
        mvMin = abs(currentVmin) < 1.;
        mvMean = abs(currentVmean) < 1.;
        mvRMS = abs(currentVRMS) < 1.;
    }
    if(autoMultimeterI){
        maMax = abs(currentVmax / seriesResistance) < 1.;
        maMin = abs(currentVmin / seriesResistance) < 1.;
        maMean = abs(currentVmean / seriesResistance) < 1.;
        maRMS = abs(currentVRMS / seriesResistance) < 1.;
    }

    if(forceMillivolts){
        mvMax = true;
        mvMin = true;
        mvMean = true;
        mvRMS = true;
    }
    if(forceMilliamps){
        maMax = true;
        maMin = true;
        maMean = true;
        maRMS = true;
    }
    if(forceKiloOhms){
        kOhms = true;
    }
    if(forceUFarads){
        uFarads = true;
    }

    if(forceVolts){
        mvMax = false;
        mvMin = false;
        mvMean = false;
        mvRMS = false;
    }
    if(forceAmps){
        maMax = false;
        maMin = false;
        maMean = false;
        maRMS = false;
    }
    if(forceOhms){
        kOhms = false;
    }
    if(forceNFarads){
        uFarads = false;
    }

    if(multimeterType == V){
        if(mvMax){
            currentVmax *= 1000;
            sendMultimeterLabel1("Max (mV)");
        }else sendMultimeterLabel1("Max (V)");

        if(mvMin){
            currentVmin *= 1000;
            sendMultimeterLabel2("Min (mV)");
        }else sendMultimeterLabel2("Min (V)");

        if(mvMean){
            currentVmean *= 1000;
            sendMultimeterLabel3("Mean (mV)");
        }else sendMultimeterLabel3("Mean (V)");

        if(mvRMS){
            currentVRMS *= 1000;
            sendMultimeterLabel4("RMS (mV)");
        }else sendMultimeterLabel4("RMS (V)");

        multimeterMax(currentVmax);
        multimeterMin(currentVmin);
        multimeterMean(currentVmean);
        multimeterRMS(currentVRMS);
        return;
    }

    if(multimeterType == I){
        if(maMax){
            currentVmax *= 1000;
            sendMultimeterLabel1("Max (mA)");
        }else sendMultimeterLabel1("Max (A)");

        if(maMin){
            currentVmin *= 1000;
            sendMultimeterLabel2("Min (mA)");
        }else sendMultimeterLabel2("Min (A)");

        if(maMean){
            currentVmean *= 1000;
            sendMultimeterLabel3("Mean (mA)");
        }else sendMultimeterLabel3("Mean (A)");

        if(maRMS){
            currentVRMS *= 1000;
            sendMultimeterLabel4("RMS (mA)");
        }else sendMultimeterLabel4("RMS (A)");


        multimeterMax(currentVmax / seriesResistance);
        multimeterMin(currentVmin / seriesResistance);
        multimeterMean(currentVmean / seriesResistance);
        multimeterRMS(currentVRMS / seriesResistance);
        return;
    }

    if(multimeterType == R){
        if(estimated_resistance!=estimated_resistance){
            estimated_resistance = 0; //Reset resistance if it's NaN
        }
        double Vm = meanVoltageLast((double)MULTIMETER_PERIOD/(double)1000, 1, 2048);
        double rtest_para_r = 1/(1/seriesResistance + 1/estimated_resistance);
        double perturbation = ch2_ref * (rtest_para_r / (R3 + R4 + rtest_para_r));
        Vm = Vm - perturbation;
        double Vin = (multimeterRsource * 2) + 3;
        double Vrat = (Vin-Vm)/Vin;
        double Rp = 1/(1/seriesResistance + 1/(R3+R4));
        estimated_resistance = ((1-Vrat)/Vrat) * Rp; //Perturbation term on V2 ignored.  V1 = Vin.  V2 = Vin(Rp/(R+Rp)) + Vn(Rtest||R / (R34 + (Rtest||R34));
        //qDebug() << "Vm = " << Vm;
        //qDebug() << "Vin = " << Vin;
        //qDebug() << "perturbation = " << perturbation;
        //qDebug() << "Vrat = " << Vrat;
        //qDebug() << "Rp = " << Rp;
        //qDebug() << "estimated_resistance = " << estimated_resistance;
        multimeterMax(0);
        multimeterMin(0);
        multimeterMean(0);

        if(autoMultimeterR){
            kOhms = (estimated_resistance) > 1000;
        }

        if(kOhms){
            estimated_resistance /= 1000;
            sendMultimeterLabel4("Resistance (kΩ)");
        }else sendMultimeterLabel4("Resistance (Ω)");
        multimeterRMS(estimated_resistance);
    }
    if(multimeterType == C){
        double cap_vbot = 0.8;
        double cap_vtop = 2.5;

        int cap_x0 = internalBuffer375_CH1->cap_x0fromLast(1, cap_vbot);
        if(cap_x0 == -1){
            qDebug() << "cap_x0 == -1";
            return;
        }
        int cap_x1 = internalBuffer375_CH1->cap_x1fromLast(1, cap_x0, cap_vbot);
        if(cap_x1 == -1){
            qDebug() << "cap_x1 == -1";
            return;
        }
        int cap_x2 = internalBuffer375_CH1->cap_x2fromLast(1, cap_x1, cap_vtop);
        if(cap_x2 == -1){
            qDebug() << "cap_x2 == -1";
            return;
        }
        qDebug() << "x0 = " << cap_x0;
        qDebug() << "x1 = " << cap_x1;
        qDebug() << "x2 = " << cap_x2;
        qDebug() << "dt = " << cap_x2-cap_x1;

        double dt = (double)(cap_x2-cap_x1)/internalBuffer375_CH1->m_samplesPerSecond;
        double Cm = -dt/(seriesResistance * log((vcc-cap_vtop)/(vcc-cap_vbot)));
        qDebug() << "Cm = " << Cm;

        if(autoMultimeterC){
            uFarads = (Cm) > 1e-6;
        }

        if(uFarads){
            sendMultimeterLabel4("Capacitance (μF)");
            Cm = Cm*1000000;
        } else {
            sendMultimeterLabel4("Capacitance (nF)");
            Cm = Cm*1000000000;
        }
        multimeterRMS(Cm);
    }

}

void isoDriver::enableMM(){
    multimeterShow = true;
}


void isoDriver::setAutoMultimeterV(bool enabled){
    autoMultimeterV = enabled;
}

void isoDriver::setAutoMultimeterI(bool enabled){
    autoMultimeterI = enabled;
}

void isoDriver::setAutoMultimeterR(bool enabled){
    autoMultimeterR = enabled;
}

void isoDriver::setAutoMultimeterC(bool enabled){
    autoMultimeterC = enabled;
}

void isoDriver::setForceMillivolts(bool enabled){
    forceMillivolts = enabled;
}

void isoDriver::setForceMilliamps(bool enabled){
    forceMilliamps = enabled;
}

void isoDriver::setForceKiloOhms(bool enabled){
    forceKiloOhms = enabled;
}

void isoDriver::setForceUFarads(bool enabled){
    forceUFarads = enabled;
}

void isoDriver::setForceVolts(bool enabled){
    forceVolts = enabled;
}

void isoDriver::setForceAmps(bool enabled){
    forceAmps = enabled;
}

void isoDriver::setForceOhms(bool enabled){
    forceOhms = enabled;
}

void isoDriver::setForceNFarads(bool enabled){
    forceNFarads = enabled;
}

void isoDriver::setSerialDecodeEnabled_CH1(bool enabled){
    serialDecodeEnabled_CH1 = enabled;
}

void isoDriver::setSerialDecodeEnabled_CH2(bool enabled){
    serialDecodeEnabled_CH2 = enabled;
}

void isoDriver::setXYmode(bool enabled){
    int graphCount = axes->graphCount();
    static QVector<bool> graphState;
    graphState.resize(graphCount);

    if(enabled)  // Hide graphs - we only want the X-Y plot to appear
    {
        xmin = 20;
        xmax = -20;
        ymin = 20;
        ymax = -20;

        for (int i=0; i < graphCount; i++)
        {
            qDebug() << "isVisible" << axes->graph(i)->visible();
            graphState[i] = axes->graph(i)->visible();
            axes->graph(i)->setVisible(false);
        }
    }
    else  // Restore State
    {
        for (int i=0; i < graphCount; i++)
        {
            qDebug() << "graphState" << graphState[i];
            axes->graph(i)->setVisible(graphState[i]);
        }
    }

    QCPCurve* curve = reinterpret_cast<QCPCurve*>(axes->plottable(0));
    curve->setVisible(enabled);
    emit enableCursorGroup(!enabled);
    XYmode = enabled;
}

void isoDriver::triggerGroupStateChange(bool enabled){
    if(enabled) sendTriggerValue((currentVmax-currentVmin)*0.85 + currentVmin);
}

void isoDriver::broadcastStats(bool CH2){
    if (CH2 && update_CH2)
    {
        update_CH2 = false;
        sendVmax_CH2(currentVmax);
        sendVmin_CH2(currentVmin);
        sendVmean_CH2(currentVmean);
        sendVRMS_CH2(currentVRMS);
    }
    else if (update_CH1)
    {
        update_CH1 = false;
        sendVmax_CH1(currentVmax);
        sendVmin_CH1(currentVmin);
        sendVmean_CH1(currentVmean);
        sendVRMS_CH1(currentVRMS);
    }
}

void isoDriver::slowTimerTick(){
    update_CH1 = true;
    update_CH2 = true;

    bool frequencyLabelVisible = false;

    if (triggerEnabled)
    {
        double triggerFrequency;
        switch(triggerMode)
        {
        case 0:
        case 1:
            triggerFrequency = (driver->deviceMode == 6) ? internalBuffer750->getTriggerFrequencyHz() : internalBuffer375_CH1->getTriggerFrequencyHz();
            break;

        case 2:
        case 3:
            triggerFrequency = internalBuffer375_CH2->getTriggerFrequencyHz();
            break;
        }

        if (triggerFrequency > 0.)
        {
            frequencyLabelVisible = true;
            siprint triggerFreqSiprint("Hz", triggerFrequency);
            siprint periodSiprint("s", 1. / triggerFrequency);

            QString cursorString = QString::asprintf(" Trigger ΔT = %s, f = %s ", periodSiprint.printVal(), triggerFreqSiprint.printVal());
            triggerFrequencyLabel->setText(cursorString);
        }
        qDebug() << triggerFrequency << "Hz";
    }

    triggerFrequencyLabel->setVisible(frequencyLabelVisible);
}

void isoDriver::setTopRange(double newTop)
{
    // NOTE: Should this be clamped to 20?
    display->topRange = newTop;
    topRangeUpdated(display->topRange);
}

void isoDriver::setBotRange(double newBot)
{
    // NOTE: Should this be clamped to 20?
    display->botRange = newBot;
    botRangeUpdated(display->botRange);
}

void isoDriver::setTimeWindow(double newWindow){
    display->window = newWindow;
    timeWindowUpdated(display->window);
}

void isoDriver::setDelay(double newDelay){
    display->delay = newDelay;
    delayUpdated(display->delay);
}

void isoDriver::takeSnapshot(QString *fileName, unsigned char channel){
    if(channel==1){
        snapshotEnabled_CH1 = true;
        QString fname_CH1 = *(fileName);
        snapshotFile_CH1 = new QFile(fname_CH1);
        qDebug() << fname_CH1;
    } else if(channel==2){
        snapshotEnabled_CH2 = true;
        QString fname_CH2 = *(fileName);
        snapshotFile_CH2 = new QFile(fname_CH2);
        qDebug() << fname_CH2;
    }
}

double isoDriver::meanVoltageLast(double seconds, unsigned char channel, int TOP){
    isoBuffer *currentBuffer;
    switch (channel){
    case 1:
        currentBuffer = internalBuffer375_CH1;
        break;
    case 2:
        currentBuffer = internalBuffer375_CH2;
        break;
    case 3:
        currentBuffer = internalBuffer750;
        break;
    }

    auto tempBuffer = currentBuffer->readBuffer(seconds, 1024, 0, 0);
    double sum = 0;
    for (const auto &sample : tempBuffer) {
        sum += currentBuffer->sampleConvert(sample, TOP, 0);
    }
    return sum / tempBuffer.size();
}

void isoDriver::rSourceChanged(int newSource){
    multimeterRsource = newSource;
}

void isoDriver::serialNeedsDisabling(int channel){
    qDebug("isoDriver acknowledges disconnect from channel %d", channel);
    mainWindowPleaseDisableSerial(channel);
}

//Thank you https://stackoverflow.com/questions/27318631/parsing-through-a-csv-file-in-qt
void isoDriver::loadFileBuffer(QFile *fileToLoad){
    //Delete the current buffer if it exists

    disableFileMode();

    if(internalBufferFile != NULL){
        delete internalBufferFile;
    }

    //Load the file
    if (!fileToLoad->open(QIODevice::ReadOnly)) {
        qDebug() << fileToLoad->errorString();
        return;
    }
    QByteArray currentLine;
    QList<QByteArray> tempList;
    //First Header line
    currentLine = fileToLoad->readLine();
    qDebug() << currentLine;

    //Averaging line
    currentLine = fileToLoad->readLine();
    qDebug() << currentLine;
    tempList.append(currentLine.split('\n'));
    tempList.append(currentLine.split('\r'));
    tempList.append(tempList.first().split(' '));
    qDebug() << tempList;
    int averages = tempList.back().toInt();
    qDebug() << averages;

    //Mode line
    tempList.clear();
    currentLine = fileToLoad->readLine();
    qDebug() << currentLine;
    tempList.append(currentLine.split('\n'));
    tempList.append(currentLine.split('\r'));
    tempList.append(tempList.first().split(' '));
    qDebug() << tempList;
    int mode = tempList.back().toInt();
    qDebug() << mode;

    tempList.clear();
    //Count the number of elements
    qulonglong numel = 0;
    while (!fileToLoad->atEnd()) {
        currentLine = fileToLoad->readLine();
        tempList.append(currentLine.split(','));
        numel += tempList.count() - 1;
        tempList.clear();
    }

    qDebug() << "There are" << numel << "elements!";

    //Prompt user for start and end times
    double defaultSampleRate = 375000;
    if(mode == 6){
        defaultSampleRate = 750000;
    }
    double minTime = ((double)averages) / defaultSampleRate;
    double maxTime = numel * ((double)averages) / defaultSampleRate;
    qDebug() << "maxTime =" << maxTime;

    daqLoadPrompt dlp(this, minTime, maxTime);
    connect(&dlp, SIGNAL(startTime(double)), this, SLOT(daqLoad_startChanged(double)));
    connect(&dlp, SIGNAL(endTime(double)), this, SLOT(daqLoad_endChanged(double)));

    //Defaults
    daqLoad_startTime = minTime;
    daqLoad_endTime = maxTime;

    dlp.exec();

    //Initialise the (modified) isoBuffer.
    int bufferLen = (int)(((daqLoad_endTime - daqLoad_startTime)/minTime) * 1.1) + 1; //Add a bit on to account for rounding error.  Int conversion rounds down, so we add 1.
    qDebug() << "daqLoad_endTime" << daqLoad_endTime;
    qDebug() << "daqLoad_startTime" << daqLoad_startTime;
    qDebug() << "minTime" << minTime;
    qDebug() << "bufferLen" << bufferLen;
    double sampleRate_Hz = defaultSampleRate/averages;
    internalBufferFile = new isoBuffer_file(this, bufferLen, sampleRate_Hz);

    //Go to start of data section
    fileToLoad->seek(0);//Return to start
    currentLine = fileToLoad->readLine();  //Chew up header
    qDebug() << currentLine;
    currentLine = fileToLoad->readLine();  //Chew up averages line
    qDebug() << currentLine;
    currentLine = fileToLoad->readLine();  //Chew up mode line
    qDebug() << currentLine;
    tempList.clear();

    //Copy the data into the (modified) isoBuffer
    float tempArray[COLUMN_BREAK + 1];  //751 elements per row with the old files; this just avoids a possible crash;
    int temp_len;

    qulonglong sampleCount = 0;
    qulonglong minCount = (daqLoad_startTime / minTime);
    qulonglong maxCount = (daqLoad_endTime / minTime);

    qDebug() << "minCount" << minCount;
    qDebug() << "maxCount" << maxCount;

    int min_i;
    int tempCount;
    qDebug() << "Loading data into isoBuffer_file";
    while (!fileToLoad->atEnd()) {
        currentLine = fileToLoad->readLine();
        tempList.append(currentLine.split(','));
        tempList.removeLast();  //Last element is a "\n", not a number.
        temp_len = 0;
        tempCount = tempList.count();
        min_i = 2000000000; //Arbitrary big number.
        for (int i=0; i<tempCount; i++){
            if((sampleCount > minCount) && (sampleCount < maxCount)){
                if(i < min_i){
                    min_i = i;
                }
                tempArray[i] = tempList.at(i).toFloat();
                temp_len++;
            }
            sampleCount++;
        }
        internalBufferFile->writeBuffer_float(&tempArray[min_i], temp_len);
        tempList.clear();
    }

    fileToLoad->close();

    qDebug() << "Initialising timer";
    //Initialise the file timer.
    if (fileTimer != NULL){
        delete fileTimer;
    }
    fileTimer = new QTimer();
    fileTimer->setTimerType(Qt::PreciseTimer);
    fileTimer->start(TIMER_PERIOD);
    connect(fileTimer, SIGNAL(timeout()), this, SLOT(fileTimerTick()));
    qDebug() << "File Buffer loaded!";
    enableFileMode();
    qDebug() << "File Mode Enabled";
}

void isoDriver::daqLoad_startChanged(double newStart){
    qDebug() << "isoDriver::daqLoad_startChanged" << newStart;
    daqLoad_startTime = newStart;
}

void isoDriver::daqLoad_endChanged(double newEnd){
    qDebug() << "isoDriver::daqLoad_endChanged" << newEnd;
    daqLoad_endTime = newEnd;
}

void isoDriver::fileTimerTick(){
    //qDebug() << "isoDriver::fileTimerTick()";
    frameActionGeneric(-2,0);
}

void isoDriver::enableFileMode(){
    fileModeEnabled = true;
    daq_maxWindowSize = daqLoad_endTime - daqLoad_startTime;
    showRealtimeButton(true);
}

void isoDriver::disableFileMode(){
    fileModeEnabled = false;
    showRealtimeButton(false);
    if(fileTimer != NULL){
        fileTimer->stop();
    }

    //Shrink screen back, if necessary.
    double mws = fileModeEnabled ? daq_maxWindowSize : ((double)MAX_WINDOW_SIZE);
    if (display->window > mws)
    {
        display->window = mws;
        timeWindowUpdated(display->window);
    }
    if ((display->window + display->delay) > mws)
    {
        display->delay -= display->window + display->delay - mws;
        delayUpdated(display->delay);
    }
    if (display->delay < 0)
    {
        display->delay = 0;
        delayUpdated(display->delay);
    }
}

void isoDriver::setSerialType(unsigned char type)
{
    serialType = type;
    qDebug() << "Serial Type changed to" << serialType;

    if(serialType == 1)
    {
        if (twoWire)
            delete twoWire;
        twoWire = new i2c::i2cDecoder(internalBuffer375_CH1, internalBuffer375_CH2, internalBuffer375_CH1->m_console1);
    }
}

void isoDriver::hideCH1(bool enable)
{
	axes->graph(0)->setVisible(!enable);
}

void isoDriver::hideCH2(bool enable)
{
	axes->graph(1)->setVisible(!enable);
}

void isoDriver::triggerStateChanged()
{
    if (!triggerEnabled)
    {
        internalBuffer375_CH1->setTriggerType(TriggerType::Disabled);
        internalBuffer375_CH2->setTriggerType(TriggerType::Disabled);
        internalBuffer750->setTriggerType(TriggerType::Disabled);
        return;
    }

    qDebug() << "triggerStateChanged()";
    switch(triggerMode)
    {
        case 0:
        {
            internalBuffer375_CH1->setTriggerType(TriggerType::Rising);
            internalBuffer375_CH2->setTriggerType(TriggerType::Disabled);
            internalBuffer750->setTriggerType(TriggerType::Rising);
            break;
        }
        case 1:
        {
            internalBuffer375_CH1->setTriggerType(TriggerType::Falling);
            internalBuffer375_CH2->setTriggerType(TriggerType::Disabled);
            internalBuffer750->setTriggerType(TriggerType::Falling);
            break;
        }
        case 2:
        {
            internalBuffer375_CH1->setTriggerType(TriggerType::Disabled);
            internalBuffer375_CH2->setTriggerType(TriggerType::Rising);
            internalBuffer750->setTriggerType(TriggerType::Disabled);
            break;

        }
        case 3:
        {
            internalBuffer375_CH1->setTriggerType(TriggerType::Disabled);
            internalBuffer375_CH2->setTriggerType(TriggerType::Falling);
            internalBuffer750->setTriggerType(TriggerType::Disabled);
            break;
        }
    }
}

void isoDriver::offsetChanged_CH1(double newOffset)
{
    m_offset_CH1 = newOffset;
}

void isoDriver::offsetChanged_CH2(double newOffset)
{
    m_offset_CH2 = newOffset;
}

void isoDriver::attenuationChanged_CH1(int attenuationIndex)
{
    switch(attenuationIndex)
    {
        case 0:
            m_attenuation_CH1 = 1;
            break;
        case 1:
            m_attenuation_CH1 = 5;
            break;
        case 2:
            m_attenuation_CH1 = 10;
            break;
        default:
            throw std::runtime_error("Unknown attenuation index for CH1");
    }
}

void isoDriver::attenuationChanged_CH2(int attenuationIndex)
{
    switch(attenuationIndex)
    {
        case 0:
            m_attenuation_CH2 = 1;
            break;
        case 1:
            m_attenuation_CH2 = 5;
            break;
        case 2:
            m_attenuation_CH2 = 10;
            break;
        default:
            throw std::runtime_error("Unknown attenuation index for CH2");
    }
}

void isoDriver::setHexDisplay_CH1(bool enabled)
{
    hexDisplay_CH1 = enabled;
}

void isoDriver::setHexDisplay_CH2(bool enabled)
{
    hexDisplay_CH2 = enabled;
}

#ifndef DISABLE_SPECTRUM

void isoDriver::setWindowingType(int windowingType)
{
    m_windowingType = windowingType;

    m_windowFactors.resize(m_asyncDFT->n_samples);
    m_windowFactorsSum = 0;
    for (int i = 0; i < m_windowFactors.size(); ++i) {
        auto factor = windowing_factor(m_windowingType, m_windowFactors.size(), i);
        m_windowFactors[i] = factor;
        m_windowFactorsSum += factor;
    }
}

void isoDriver::setMinFreqResp(double minFreqResp)
{
    m_freqRespMin = minFreqResp;
    m_freqRespFlag = true;
}

void isoDriver::setMaxFreqResp(double maxFreqResp)
{
    m_freqRespMax = maxFreqResp;
    m_freqRespFlag = true;
}

void isoDriver::retickXAxis()
{
    double lower_range = display->leftRange;
    double upper_range = display->rightRange;

    if(display->logSpaceX) {
        axes->xAxis->setScaleType(QCPAxis::stLogarithmic);
    } else {
        axes->xAxis->setScaleType(QCPAxis::stLinear);
    }
    if(display->logSpaceX) {
        if((upper_range/lower_range)>100.0) {
            axes->xAxis->setScaleLogBase(10.0);
            axes->xAxis->setSubTickCount(9);
            fSpaceLabel->setVisible(false);
            axes->xAxis->setNumberPrecision(0);
            axes->xAxis->setNumberFormat("gb");
        } else if ((upper_range/lower_range)>10.0) {
            axes->xAxis->setScaleLogBase(pow(10.0,1./4));
            axes->xAxis->setSubTickCount(0);
            fSpaceLabel->setText(QString::asprintf("freq. spacing: x%.2f", pow(10.0,1./4)));
            fSpaceLabel->setVisible(true);
            axes->xAxis->setNumberPrecision(0);
            axes->xAxis->setNumberFormat("f");
        } else {
            // round the inverse of the log10 power to the closest power of two, then divide by 4.  e.g, upper_range/lower_range = 10^(1/3.5) yields 10^(1/4/4)=10^(1/16)
            int log2_inv_log10 = round(log2(1./log10(upper_range/lower_range)));
            axes->xAxis->setScaleLogBase(pow(10.0,1./(pow(2,log2_inv_log10)*4.)));
            axes->xAxis->setSubTickCount(0);
            fSpaceLabel->setText(QString::asprintf("freq. spacing: x%.2f", pow(10.0,1./(pow(2,log2_inv_log10)*4.))));
            fSpaceLabel->setVisible(true);
            if(lower_range>10.0) {
                axes->xAxis->setNumberPrecision(0);
            } else {
                axes->xAxis->setNumberPrecision(1);
            }
            axes->xAxis->setNumberFormat("f");
        }
    } else {
        fSpaceLabel->setVisible(false);
        axes->xAxis->setAutoTickCount(6);
        axes->xAxis->setNumberPrecision(3);
        axes->xAxis->setNumberFormat("gb");
    }
}

void isoDriver::logSpacingEnableHor(bool horLogSpace)
{
    if(display->leftRange<1.0) {
        display->leftRange = 1.0;
    }
    display->logSpaceX = horLogSpace;
    this->retickXAxis();
}


void isoDriver::setFreqRespStep(double freqRespStep)
{
    m_freqRespStep = freqRespStep;
    m_freqRespFlag = true;
}

void isoDriver::setFreqRespType(int freqRespType)
{
    m_freqRespType = freqRespType;
}

void isoDriver::restartFreqResp()
{
    m_freqRespFlag = true;
}
#endif

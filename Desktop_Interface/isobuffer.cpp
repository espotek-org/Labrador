#include "isobuffer.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <iostream>

#include "isodriver.h"
#include "uartstyledecoder.h"

namespace
{
constexpr char const* fileHeaderFormat =
    "EspoTek Labrador DAQ V1.0 Output File\n"
    "Averaging = %d\n"
    "Mode = %d\n";

constexpr auto kSamplesSeekingCap = 20;

#ifdef INVERT_MM
constexpr auto fX0Comp = std::greater<int> {};
constexpr auto fX1X2Comp = std::less<int> {};
#else
constexpr auto fX0Comp = std::less<int> {};
constexpr auto fX1X2Comp = std::greater<int> {};
#endif

constexpr auto kTopMultimeter = 2048;
constexpr double kTriggerSensitivityMultiplier = 4;
}

isoBuffer::isoBuffer(QWidget* parent, int bufferLen, int windowLen, isoDriver* caller, unsigned char channel_value)
    : QWidget(parent)
    , m_channel(channel_value)
    , m_bufferPtr(std::make_unique<short[]>(bufferLen*2))
    , m_bufferLen(bufferLen)
    , m_window_capacity(windowLen)
    , m_samplesPerSecond(bufferLen/21.0/375*VALID_DATA_PER_375)
    , m_sampleRate_bit(bufferLen/21.0/375*VALID_DATA_PER_375*8)
    , m_virtualParent(caller)
{
    m_buffer = m_bufferPtr.get();
    m_window.reserve(m_window_capacity);
    m_window_iter = m_window.begin();
}

void isoBuffer::insertIntoBuffer(short item)
{
    m_buffer[m_back] = item;
    m_buffer[m_back+m_bufferLen] = item;
    m_back++;
    m_insertedCount++;

    if (m_insertedCount > m_bufferLen)
    {
        m_insertedCount = m_bufferLen;
    }

    if (m_back == m_bufferLen)
    {
        m_back = 0;
    }

    /* Fill-in time domain window */
    if (m_window.size() < m_window_capacity) {
        m_window.push_back(item);
    } else {
        *m_window_iter++ = item;
        if (m_window_iter == m_window.end()) {
            m_window_iter = m_window.begin();
        }
    }

    /* Fill-in freqResp buffer */
    if(m_freqRespActive)
    {
        if (freqResp_count >= freqResp_samples)
        {
            /* Shift freqResp buffer once by removing first element and adding an element to the end */
            freqResp_buffer.pop_front();
            freqResp_buffer.push_back(item);
            freqResp_count = freqResp_samples;
        }
        else
        {
            freqResp_buffer.push_back(item);
        }
        /* Update number of freqResp samples */
        freqResp_count++;
    }

    checkTriggered();
}

short isoBuffer::bufferAt(uint32_t idx) const
{
    if (idx > m_insertedCount)
        qFatal("isoBuffer::bufferAt: invalid query, idx = %" PRIu32 ", m_insertedCount = %" PRIu32, idx, m_insertedCount);

    return m_buffer[(m_back-1) + m_bufferLen - idx];
}

template<typename T, typename Function>
void isoBuffer::writeBuffer(T* data, int len, int TOP, Function transform)
{
    for (int i = 0; i < len; ++i)
    {
        insertIntoBuffer(transform(data[i]));
    }

    // Output to CSV
    if (m_fileIOEnabled)
    {
        bool isUsingAC = m_channel == 1
                         ? m_virtualParent->AC_CH1
                         : m_virtualParent->AC_CH2;

        for (int i = 0; i < len && m_fileIOEnabled; i++)
        {
            double convertedSample = sampleConvert(data[i], TOP, isUsingAC);

            maybeOutputSampleToFile(convertedSample);
        }
    }
}

void isoBuffer::writeBuffer_char(char* data, int len)
{
    writeBuffer(data, len, 128, [](char item) -> short {return item;});
}

void isoBuffer::writeBuffer_short(short* data, int len)
{
    writeBuffer(data, len, 2048, [](short item) -> short {return item >> 4;});
}

std::unique_ptr<short[]> isoBuffer::readBuffer(double sampleWindow, int numSamples, bool singleBit, double delayOffset)
{
    /*
     * The expected behavior is to run backwards over the buffer with a stride
     * of timeBetweenSamples steps, and push the touched elements into readData.
     * When timeBetweenSamples steps don't land on integer boundaries,
     * value is estimated by linearly interpolating neighboring boundaries.
     * If more elements are requested than how many are stored (1), the buffer
     * will be populated only partially. Modifying this function to return null
     * or a zero-filled buffer instead should be simple enough.
     *
     * (1) m_insertedCount < (delayOffset + sampleWindow) * m_samplesPerSecond
     */
    const double timeBetweenSamples = sampleWindow * m_samplesPerSecond / numSamples;
    const int delaySamples = delayOffset * m_samplesPerSecond;

    std::unique_ptr<short[]> readData = std::make_unique<short[]>(numSamples);

    std::fill (readData.get(), readData.get() + numSamples, short(0));

    double itr = delaySamples, itr_lb, itr_ub;
    short data_lb, data_ub;
    for (int i = 0; i < numSamples && itr < m_insertedCount; i++)
    {
        assert(int(itr) >= 0);
        itr_lb = floor(itr);
        itr_ub = ceil(itr);
        data_lb = bufferAt(int(itr_lb));
        data_ub = bufferAt(int(itr_ub));
        readData[i] = data_lb + short(round(((data_ub-data_lb)*(itr-itr_lb))/(itr_ub-itr_lb)));

        if (singleBit)
        {
            int subIdx = 8*(-itr-floor(-itr));
            readData[i] = data_lb & (1 << subIdx);
        }

        itr += timeBetweenSamples;
    }

    return readData;
}

std::vector<short> isoBuffer::readWindow()
{
    std::vector<short> readData(m_window.size());
    std::copy(m_window.begin(), m_window_iter, std::copy(m_window_iter, m_window.end(), readData.begin()));
    return readData;
}

void isoBuffer::clearBuffer()
{
    for (uint32_t i = 0; i < m_bufferLen; i++)
    {
        m_buffer[i] = 0;
        m_buffer[i + m_bufferLen] = 0;
    }

    m_back = 0;
    m_insertedCount = 0;

    m_window.clear();
    m_window_iter = m_window.begin();
}

void isoBuffer::gainBuffer(int gain_log)
{
    qDebug() << "Buffer shifted by" << gain_log;
    for (uint32_t i = 0; i < m_bufferLen; i++)
    {
        if (gain_log < 0)
        {
            m_buffer[i] <<= -gain_log;
            m_buffer[i+m_bufferLen] <<= -gain_log;
        }
        else
        {
            m_buffer[i] >>= gain_log;
            m_buffer[i+m_bufferLen] >>= gain_log;
        }
    }
}

void isoBuffer::enableFreqResp(bool enable, double freqValue)
{
    m_freqRespActive = enable;
    freqResp_samples = uint32_t(m_samplesPerSecond/freqValue);
}

void isoBuffer::outputSampleToFile(double averageSample)
{
    char numStr[32];
    sprintf(numStr,"%7.5f, ", averageSample);

    m_currentFile->write(numStr);
    m_currentColumn++;

    if (m_currentColumn == COLUMN_BREAK)
    {
        m_currentFile->write("\n");
        m_currentColumn = 0;
    }
}

void isoBuffer::maybeOutputSampleToFile(double convertedSample)
{
    /*
     * This function adds a sample to an accumulator and bumps a sample count.
     * After the sample count hits some threshold the samples are averaged
     * and the average is written to a file.
     * If this makes us hit the max. file size, then fileIO is disabled.
     */

    m_fileIO_sampleAccumulator += convertedSample;
    m_fileIO_sampleCount++;

    if (m_fileIO_sampleCount == m_fileIO_sampleCountPerWrite)
    {
        double averageSample = m_fileIO_sampleAccumulator / m_fileIO_sampleCount;
        outputSampleToFile(averageSample);

        // Reset the accumulator and sample count for next data point.
        m_fileIO_sampleAccumulator = 0;
        m_fileIO_sampleCount = 0;

        // value of 0 means "no limit", meaning we must skip the check by returning.
        if (m_fileIO_maxFileSize == 0)
            return;

        // 7 chars(number) + 1 char(comma) + 1 char(space) = 9 bytes/sample.
        m_fileIO_numBytesWritten += 9;
        if (m_fileIO_numBytesWritten >= m_fileIO_maxFileSize)
        {
            m_fileIOEnabled = false; // Just in case signalling fails.
            fileIOinternalDisable();
        }
    }
}

void isoBuffer::enableFileIO(QFile* file, int samplesToAverage, qulonglong max_file_size)
{

    // Open the file
    file->open(QIODevice::WriteOnly);
    m_currentFile = file;

    // Add the header
    char headerLine[256];
    sprintf(headerLine, fileHeaderFormat, samplesToAverage, m_virtualParent->driver->deviceMode);
    m_currentFile->write(headerLine);

    // Set up the isoBuffer for DAQ
    m_fileIO_maxFileSize = max_file_size;
    m_fileIO_sampleCountPerWrite = samplesToAverage;
    m_fileIO_sampleCount = 0;
    m_fileIO_sampleAccumulator = 0;
    m_fileIO_numBytesWritten = 0;

    // Enable DAQ
    m_fileIOEnabled = true;

    qDebug("File IO enabled, averaging %d samples, max file size %lluMB", samplesToAverage, max_file_size/1000000);
    qDebug() << max_file_size;
}

void isoBuffer::disableFileIO()
{
    m_fileIOEnabled = false;
    m_currentColumn = 0;
    m_currentFile->close();
    return;
}


double isoBuffer::sampleConvert(short sample, int TOP, bool AC) const
{
    double scope_gain = (double)(m_virtualParent->driver->scopeGain);

    double voltageLevel = (sample * (vcc/2)) / (m_frontendGain*scope_gain*TOP);
    if (m_virtualParent->driver->deviceMode != 7) voltageLevel += m_voltage_ref;
#ifdef INVERT_MM
    if (m_virtualParent->driver->deviceMode == 7) voltageLevel *= -1;
#endif

    if (AC)
    {
        // This is old (1 frame in past) value and might not be good for signals with
        // large variations in DC level (although the cap should filter that anyway)??
        voltageLevel -= m_virtualParent->currentVmean;
    }
    return voltageLevel;
}

short isoBuffer::inverseSampleConvert(double voltageLevel, int TOP, bool AC) const
{
    double scope_gain = m_virtualParent->driver->scopeGain;

    if (AC)
    {
        // This is old (1 frame in past) value and might not be good for signals with
        // large variations in DC level (although the cap should filter that anyway)??
        voltageLevel += m_virtualParent->currentVmean;
    }

#ifdef INVERT_MM
    if (m_virtualParent->driver->deviceMode == 7) voltageLevel *= -1;
#endif
    if (m_virtualParent->driver->deviceMode != 7) voltageLevel -= m_voltage_ref;

    // voltageLevel = (sample * (vcc/2)) / (frontendGain*scope_gain*TOP);
    short sample = (voltageLevel * (m_frontendGain*scope_gain*TOP))/(vcc/2);
    return sample;
}

// For capacitance measurement.
// x0, x1 and x2 are all various time points used to find the RC coefficient.
template<typename Function>
int isoBuffer::capSample(int offset, int target, double seconds, double value, Function comp)
{
    int samples = seconds * m_samplesPerSecond;

    if (static_cast<int32_t>(m_back) < (samples + offset))
        return -1;

    short sample = inverseSampleConvert(value, 2048, 0);

    int found = 0;
    for (int i = samples + offset; i--;)
    {
        short currentSample = bufferAt(i);
        if (comp(currentSample, sample))
            found = found + 1;
        else
            found = std::max(0, found-1);

        if (found > target)
            return samples - i;
    }

    return -1;
}

int isoBuffer::cap_x0fromLast(double seconds, double vbot)
{
    return capSample(0, kSamplesSeekingCap, seconds, vbot, fX0Comp);
}

int isoBuffer::cap_x1fromLast(double seconds, int x0, double vbot)
{
    return capSample(-x0, kSamplesSeekingCap, seconds, vbot, fX1X2Comp);
}

int isoBuffer::cap_x2fromLast(double seconds, int x1, double vtop)
{
    return capSample(-x1, kSamplesSeekingCap, seconds, vtop, fX1X2Comp);
}

void isoBuffer::serialManage(double baudRate, UartParity parity, bool hexDisplay)
{
    if (m_decoder == NULL)
    {
        m_decoder = new uartStyleDecoder(baudRate, this);
    }
    if (!m_isDecoding)
    {
        m_decoder->m_updateTimer.start(CONSOLE_UPDATE_TIMER_PERIOD);
        m_isDecoding = true;
    }

    m_decoder->m_baudRate = baudRate;
    m_decoder->setParityMode(parity);
    m_decoder->setHexDisplay(hexDisplay);
    m_decoder->serialDecode();
}

void isoBuffer::setTriggerType(TriggerType newType)
{
    qDebug() << "Trigger Type: " << (uint8_t)newType;
    m_triggerType = newType;
    m_lastTriggerDetlaT = 0;
}

void isoBuffer::setTriggerLevel(double voltageLevel, uint16_t top, bool acCoupled)
{
    m_triggerLevel = inverseSampleConvert(voltageLevel, top, acCoupled);
    m_triggerSensitivity = static_cast<short>(1 + abs(voltageLevel * kTriggerSensitivityMultiplier * static_cast<double>(top) / 128.));
    qDebug() << "Trigger Level: " << m_triggerLevel;
    qDebug() << "Trigger sensitivity:" << m_triggerSensitivity;
    m_lastTriggerDetlaT = 0;
}

// TODO: Clear trigger
// FIXME: AC changes will not be reflected here
void isoBuffer::checkTriggered()
{
    if (m_triggerType == TriggerType::Disabled)
        return;

    if ((bufferAt(0) >= (m_triggerLevel + m_triggerSensitivity)) && (m_triggerSeekState == TriggerSeekState::BelowTriggerLevel))
    {
        // Rising Edge
        m_triggerSeekState = TriggerSeekState::AboveTriggerLevel;
        if (m_triggerType == TriggerType::Rising)
            addTriggerPosition(m_back - 1);
    }
    else if ((bufferAt(0) < (m_triggerLevel - m_triggerSensitivity)) && (m_triggerSeekState == TriggerSeekState::AboveTriggerLevel))
    {
        // Falling Edge
        m_triggerSeekState = TriggerSeekState::BelowTriggerLevel;
        if (m_triggerType == TriggerType::Falling)
            addTriggerPosition(m_back - 1);
    }
}

double isoBuffer::getDelayedTriggerPoint(double delay)
{
    if (m_triggerPositionList.size() == 0)
        return 0;

    const uint32_t delaySamples = delay * m_samplesPerSecond;

    auto isValid = [=](uint32_t index)->bool
    {
        if (m_back > delaySamples)
            return (index < m_back - delaySamples) || (index >= m_back);
        else
            return (index < m_bufferLen + m_back - delaySamples) && (index >= m_back);
    };

    auto getDelay = [=](uint32_t index)->double
    {
        if (m_back > index)
            return (m_back - index) / static_cast<double>(m_samplesPerSecond);
        else
            return (m_bufferLen + (m_back - 1) - index) / static_cast<double>(m_samplesPerSecond);
    };

    // Like std::find_if but returns the last element matching the predicate instead of the first one
    // TODO: Move this elsewhere (maybe a utils / algorithms file??)
    // requires first and last to be Bidirectional iters, and form a valid range
    // requires p to be a valid unaryPredicate
    // Full signature would be:
    // template<typename It, typename Predicate>
    // It find_last_if(It begin, It end, Predicate p)
    auto find_last_if = [](auto begin, auto end, auto p)
    {
        using It = decltype(begin); // TODO: remove this line once this is a proper function
        std::reverse_iterator<It> rlast(begin), rfirst(end);
        auto found = std::find_if(rfirst, rlast, p);
        return found == rlast
               ? end
               : std::prev(found.base());
    };

    auto it = find_last_if(m_triggerPositionList.begin(), m_triggerPositionList.end(), isValid);
    if (it != m_triggerPositionList.end())
    {
        // NOTE: vector::erase does not remove the element pointed to by the second iterator.
        m_triggerPositionList.erase(m_triggerPositionList.begin(), it);
        return getDelay(m_triggerPositionList[0]);
    }

    return 0;
}

double isoBuffer::getTriggerFrequencyHz()
{
    return (m_lastTriggerDetlaT == 0) ? -1. : static_cast<double>(m_samplesPerSecond) / static_cast<double>(m_lastTriggerDetlaT);
}

void isoBuffer::addTriggerPosition(uint32_t position)
{
    static uint32_t s_lastPosition = 0;
    m_triggerPositionList.push_back(m_back - 1);
    m_lastTriggerDetlaT = (position > s_lastPosition) ? (position - s_lastPosition) : position + m_bufferLen - s_lastPosition;

    s_lastPosition = position;

    //qDebug() << position << s_lastPosition << static_cast<double>(m_samplesPerSecond) / static_cast<double>(m_lastTriggerDetlaT) << "Hz";
}

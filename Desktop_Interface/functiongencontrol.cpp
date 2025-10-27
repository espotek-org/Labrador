#include "functiongencontrol.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>

namespace functionGen {

ChannelData const& SingleChannelController::getData() const {
	return m_data;
}

void SingleChannelController::waveformName(QString newName)
{
    qDebug() << "newName = " << newName;
    m_data_orig.waveform = newName;

#if defined(PLATFORM_ANDROID)
    QFile file(newName.prepend("assets:/waveforms/").append(".tlw"));
#else
    QFile file(QStandardPaths::locate(QStandardPaths::AppDataLocation, newName.prepend("waveforms/").append(".tlw")));
#endif

    qDebug() << "opening" << file.fileName();
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("could not open %s", qUtf8Printable(file.fileName()));

    int length = file.readLine().trimmed().toInt();
    m_data_orig.divisibility = file.readLine().trimmed().toInt();
    QByteArray data = file.readLine().trimmed();
    file.close();

    qDebug() << "Length = " << length;
    qDebug() << "Divisibility = " << m_data_orig.divisibility;

    // Length is redundant, could be derived from the sample list.
    if (length != data.count('\t') + 1)
        qFatal("%s: sample count mismatch", qUtf8Printable(file.fileName()));
    m_data_orig.samples.resize(length);

    data.replace('\t', '\0');
    const char *dataString = data.constData();
    QByteArray dataElem;
    for (auto &sample : m_data_orig.samples) {
        dataElem.setRawData(dataString, strlen(dataString));
        sample = static_cast<uint8_t>(dataElem.toInt());
        dataString += dataElem.size() + 1;
    }
    m_data_orig.repeat_forever = true;

	double newMaxFreq = DAC_SPS / (length >> (m_data_orig.divisibility - 1));
	double newMinFreq = double(CLOCK_FREQ) / 1024.0 / 65535.0 / static_cast<double>(length);

	setMaxFreq(newMaxFreq);
	setMinFreq(newMinFreq);

    notifyUpdate(this);
}

void SingleChannelController::freqUpdate(double newFreq)
{
	qDebug() << "newFreq = " << newFreq;
	m_data_orig.freq = newFreq;
	m_data_orig.repeat_forever = true;
	notifyUpdate(this);
}

void SingleChannelController::reinitWfData()
{
    m_data = m_data_orig;
}

unsigned char SingleChannelController::doScalingForTripleMode(unsigned char fGenTriple, ChannelID channelID)
{
    //Triple mode
    if ((m_data.amplitude + m_data.offset) > FGEN_LIMIT)
	{
        m_data.amplitude /= 3.0;
        m_data.offset /= 3.0;
        fGenTriple |= static_cast<uint8_t>(!static_cast<uint8_t>(channelID) + 1);
    }
    else
	{
		fGenTriple &= static_cast<uint8_t>(254 - !static_cast<uint8_t>(channelID));
	}
    return fGenTriple;
}

void SingleChannelController::scaleToDAC()
{
    //Waveform scaling in V
    m_data.amplitude = (m_data.amplitude * 255) / FGEN_LIMIT;
    m_data.offset = (m_data.offset * 255) / FGEN_LIMIT;
    if (m_data.offset < FGEN_OFFSET)
	{
        if (m_data.amplitude > 5)
            m_data.amplitude -= FGEN_OFFSET;
        else
            m_data.amplitude = 0;
        m_data.offset = FGEN_OFFSET;
    }
    // Apply duty cycle to Square waveform
    if(m_data.waveform == "Square")
    {
        int length = m_data.samples.size();
        int dutyCycle = static_cast<int>((m_data.dutyCycle*length)/100);
        for (int i = 0; i < length; ++i)
        {
            if(i < dutyCycle)
                m_data.samples[i] = 255;
            else
                m_data.samples[i] = 0;
        }
    }

	auto applyAmplitudeAndOffset = [&](unsigned char sample) -> unsigned char
	{
		return sample / 255.0 * m_data.amplitude + m_data.offset;
	};

	std::transform(m_data.samples.begin(), m_data.samples.end(),
	               m_data.samples.begin(), // transform in place
	               applyAmplitudeAndOffset);
}


//Need to increase size of wave if its freq too high, or too low!
void SingleChannelController::resizeWaveform()
{
    int shift = 0;
    int newLength = m_data.samples.size();

    while ((newLength >> shift) * m_data.freq > DAC_SPS)
        shift++;

    if (shift != 0)
    {
        m_data.divisibility -= shift;
        newLength >>= shift;

        for (int i = 0; i < newLength; ++i)
            m_data.samples[i] = m_data.samples[i << shift];

        m_data.samples.resize(newLength);
        m_data.samples.shrink_to_fit();

        if (m_data.divisibility <= 0)
            qDebug("SingleChannelController::setFunctionGen: channel divisibility <= 0 after T-stretching");
    }
}

void SingleChannelController::getClockSettings(int* clkSetting, int* timerPeriod)
{
    // Timer Setup
    int validClockDivs[7] = {1, 2, 4, 8, 64, 256, 1024};
	auto period = [&](int division) -> int
	{
		return CLOCK_FREQ / (division * m_data.samples.size() * m_data.freq) - 0.5;
	};

	int* clkSettingIt = std::find_if(std::begin(validClockDivs), std::end(validClockDivs),
	                                 [&](int division) -> bool { return period(division) < 65535; });

    *timerPeriod = period(*clkSettingIt);

	// +1 to change from [0:n) to [1:n]
    *clkSetting = std::distance(std::begin(validClockDivs), clkSettingIt) + 1;

    this->notifyFreqUpdate(*clkSetting,*timerPeriod,m_data.samples.size());
}



void SingleChannelController::amplitudeUpdate(double newAmplitude)
{
	qDebug() << "newAmplitude = " << newAmplitude;
	m_data_orig.amplitude = newAmplitude;
	m_data_orig.repeat_forever = true;
	notifyUpdate(this);
}

void SingleChannelController::offsetUpdate(double newOffset)
{
	qDebug() << "newOffset = " << newOffset;
	m_data_orig.offset = newOffset;
	m_data_orig.repeat_forever = true;
	notifyUpdate(this);
}

void SingleChannelController::dutyCycleUpdate(double newDutyCycle)
{
	qDebug() << "newDutyCycle = " << newDutyCycle;
	m_data_orig.dutyCycle = newDutyCycle;
	m_data_orig.repeat_forever = true;
	notifyUpdate(this);
}

void SingleChannelController::txuartUpdate(int baudRate, std::vector<uint8_t> samples)
{
	// Update txUart data
	int length = samples.size();
	m_data_orig.samples.resize(length);
	m_data_orig.samples = samples;
	m_data_orig.freq = baudRate/length;
	m_data_orig.repeat_forever = false;

	notifyUpdate(this);
}

void SingleChannelController::backup_waveform()
{
	m_data_orig.freq2 = m_data_orig.freq;
}

void SingleChannelController::restore_waveform()
{
	m_data_orig.freq = m_data_orig.freq2;
	waveformName(m_data_orig.waveform);
}


DualChannelController::DualChannelController(QWidget *parent) : QLabel(parent)
{
	// A bunch of plumbing to forward the SingleChannelController's signals

	SingleChannelController* controller1 = getChannelController(ChannelID::CH1);
	SingleChannelController* controller2 = getChannelController(ChannelID::CH2);

	connect(controller1, &SingleChannelController::notifyUpdate,
	        this, [=](SingleChannelController* ptr){ this->functionGenToUpdate(ChannelID::CH1, ptr); });

	connect(controller1, &SingleChannelController::notifyFreqUpdate,
	        this, [=](int newClkSetting, int newTimerPeriod, int wfSize){ this->freqUpdated(ChannelID::CH1,newClkSetting,newTimerPeriod,wfSize); });

	connect(controller1, &SingleChannelController::setMaxFreq,
	        this, &DualChannelController::setMaxFreq_CH1);

	connect(controller1, &SingleChannelController::setMinFreq,
	        this, &DualChannelController::setMinFreq_CH1);

	connect(controller2, &SingleChannelController::notifyUpdate,
	        this, [=](SingleChannelController* ptr){ this->functionGenToUpdate(ChannelID::CH2, ptr); });

	connect(controller2, &SingleChannelController::notifyFreqUpdate,
	        this, [=](int newClkSetting, int newTimerPeriod, int wfSize){ this->freqUpdated(ChannelID::CH2,newClkSetting,newTimerPeriod, wfSize); });

	connect(controller2, &SingleChannelController::setMaxFreq,
	        this, &DualChannelController::setMaxFreq_CH2);

	connect(controller2, &SingleChannelController::setMinFreq,
	        this, &DualChannelController::setMinFreq_CH2);

    this->hide();
}


SingleChannelController* DualChannelController::getChannelController(ChannelID channelID)
{
	return &m_channels[(int)channelID];
}

// The rest of this file is just plumbing to forward slot calls to SingleChannelController's
// Hopefuly it can be mostly removed eventually
void DualChannelController::waveformName(ChannelID channelID, QString newName)
{
	getChannelController(channelID)->waveformName(newName);
}

void DualChannelController::freqUpdate(ChannelID channelID, double newFreq)
{
	getChannelController(channelID)->freqUpdate(newFreq);
}

void DualChannelController::amplitudeUpdate(ChannelID channelID, double newAmplitude)
{
	getChannelController(channelID)->amplitudeUpdate(newAmplitude);
}

void DualChannelController::offsetUpdate(ChannelID channelID, double newOffset)
{
	getChannelController(channelID)->offsetUpdate(newOffset);
}

void DualChannelController::dutyCycleUpdate(ChannelID channelID, double newDutyCycle)
{
	getChannelController(channelID)->dutyCycleUpdate(newDutyCycle);
}

void DualChannelController::txuartUpdate(ChannelID channelID, int baudRate, std::vector<uint8_t> samples)
{
	getChannelController(channelID)->txuartUpdate(baudRate, samples);
}

void DualChannelController::backup_waveform(ChannelID channelID)
{
	getChannelController(channelID)->backup_waveform();
}

void DualChannelController::restore_waveform(ChannelID channelID)
{
	getChannelController(channelID)->restore_waveform();
}


void DualChannelController::waveformName_CH1(QString newName)
{
	waveformName(ChannelID::CH1, newName);
}

void DualChannelController::freqUpdate_CH1(double newFreq)
{
	freqUpdate(ChannelID::CH1, newFreq);
}

void DualChannelController::amplitudeUpdate_CH1(double newAmplitude)
{
	amplitudeUpdate(ChannelID::CH1, newAmplitude);
}

void DualChannelController::offsetUpdate_CH1(double newOffset)
{
	offsetUpdate(ChannelID::CH1, newOffset);
}

void DualChannelController::dutyCycleUpdate_CH1(double newDutyCycle)
{
	dutyCycleUpdate(ChannelID::CH1, newDutyCycle);
}


void DualChannelController::waveformName_CH2(QString newName)
{
	waveformName(ChannelID::CH2, newName);
}

void DualChannelController::freqUpdate_CH2(double newFreq)
{
	freqUpdate(ChannelID::CH2, newFreq);
}

void DualChannelController::amplitudeUpdate_CH2(double newAmplitude)
{
	amplitudeUpdate(ChannelID::CH2, newAmplitude);
}

void DualChannelController::offsetUpdate_CH2(double newOffset)
{
	offsetUpdate(ChannelID::CH2, newOffset);
}

void DualChannelController::dutyCycleUpdate_CH2(double newDutyCycle)
{
    dutyCycleUpdate(ChannelID::CH2, newDutyCycle);
}

}


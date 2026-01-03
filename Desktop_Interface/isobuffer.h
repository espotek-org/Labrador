#ifndef ISOBUFFER_H
#define ISOBUFFER_H

// TODO: Move headers used only in implementation to isobuffer.cpp
#include <list>
#include <memory>
#include <vector>

#include <QWidget>
#include <QString>
#include <QByteArray>
#include <QDebug>
#include <QTimer>
#include <QPlainTextEdit>
#include <QDir>
#include <QFile>

#include "xmega.h"
#include "desktop_settings.h"
#include "genericusbdriver.h"

class isoDriver;
class uartStyleDecoder;
enum class UartParity : uint8_t;

enum class TriggerType : uint8_t
{
    Disabled,
    Rising,
    Falling,
    CH1SigGen,
    CH2SigGen
};

enum class TriggerSeekState : uint8_t
{
    BelowTriggerLevel,
    AboveTriggerLevel
};

// isoBuffer is a generic class that enables O(1) read times (!!!) on all
// read/write operations, while maintaining a huge buffer size.
// Imagine it as a circular buffer, but with access functions specifically
// designed for isochronous data from an Xmega.

constexpr uint32_t CONSOLE_UPDATE_TIMER_PERIOD = ISO_PACKETS_PER_CTX * 4;

// TODO: Make private what should be private
// TODO: Change integer types to cstdint types
class isoBuffer : public QWidget
{
	Q_OBJECT
public:
#ifndef DISABLE_SPECTRUM
	isoBuffer(QWidget* parent = 0, int bufferLen = 0, int windowLen = 0, isoDriver* caller = 0, unsigned char channel_value = 0);
#else
	isoBuffer(QWidget* parent = 0, int bufferLen = 0, isoDriver* caller = 0, unsigned char channel_value = 0);
#endif
	~isoBuffer() = default;

//	Basic buffer operations
	short bufferAt(uint32_t idx) const;
	void insertIntoBuffer(short item);
	void clearBuffer();
	void gainBuffer(int gain_log);

#ifndef DISABLE_SPECTRUM
    void enableFreqResp(bool enable, double freqValue);
#endif

// Advanced buffer operations
private:
    template<typename T, typename Function>
    void writeBuffer(T* data, int len, int TOP, Function transform);
public:
	void writeBuffer_char(char* data, int len);
	void writeBuffer_short(short* data, int len);

    std::vector<short> readBuffer(double sampleWindow, int numSamples, bool singleBit, double delayOffset);
#ifndef DISABLE_SPECTRUM
    std::vector<short> readWindow();
#endif
//	file I/O
private:
	void outputSampleToFile(double averageSample);
	void maybeOutputSampleToFile(double convertedSample);
public:
	double sampleConvert(short sample, int TOP, bool AC) const;
	short inverseSampleConvert(double voltageLevel, int TOP, bool AC) const;

private:
	template<typename Function>
	int capSample(int offset, int target, double seconds, double value, Function comp);
    void checkTriggered();
public:
	int cap_x0fromLast(double seconds, double vbot);
	int cap_x1fromLast(double seconds, int x0, double vbot);
	int cap_x2fromLast(double seconds, int x1, double vtop);
	void serialManage(double baudRate, UartParity parity, bool hexDisplay);
    void setTriggerType(TriggerType newType);
    void setTriggerLevel(double voltageLevel, uint16_t top, bool acCoupled);
    double getDelayedTriggerPoint(double delay);
    double getTriggerFrequencyHz();
    void setSigGenTriggerFreq(functionGen::ChannelID channelID, int clkSetting, int timerPeriod, int wfSize);

// ---- MEMBER VARIABLES ----

//	Presentation?
// TODO: Add consoles as constructor arguments
// NOTE: These are initialized in mainwindow.cpp
	QPlainTextEdit* m_console1;
	QPlainTextEdit* m_console2;
	unsigned char m_channel = 255;
	bool m_serialAutoScroll = true;

//	Internal Storage
    std::unique_ptr<short[]> m_bufferPtr;
    short* m_buffer;
	uint32_t m_back = 0;
	uint32_t m_insertedCount = 0;
	uint32_t m_bufferLen;

#ifndef DISABLE_SPECTRUM
private:
    // Time domain samples for spectrum view
    std::vector<short> m_window;
    std::vector<short>::size_type m_window_capacity;
    std::vector<short>::iterator m_window_iter;

    bool m_freqRespActive = false;
public:
    std::list<short> freqResp_buffer;
    uint32_t freqResp_count = 0;
    uint32_t freqResp_samples = 0;
#endif

// Conversion And Sampling
	double m_voltage_ref = 1.65;
	double m_frontendGain = (R4 / (R3 + R4));
	int m_samplesPerSecond;
	int m_sampleRate_bit;
    TriggerType m_triggerType = TriggerType::Disabled;
    TriggerSeekState m_triggerSeekState = TriggerSeekState::BelowTriggerLevel;
    short m_triggerLevel = 0;
    short m_triggerSensitivity = 0;
    std::vector<uint32_t> m_triggerPositionList = {};
//	UARTS decoding
	uartStyleDecoder* m_decoder = NULL;
	bool m_isDecoding = true;
private:
//	File I/O
	bool m_fileIOEnabled = false;
	QFile* m_currentFile;
	int m_fileIO_sampleCountPerWrite;
	int m_fileIO_sampleCount;
	double m_fileIO_sampleAccumulator;
	qulonglong m_fileIO_maxFileSize;
	qulonglong m_fileIO_numBytesWritten;
	unsigned int m_currentColumn = 0;
    double m_lastTriggerDeltaT = 0.0;
    float bufferSamplesPerCH1WfCycle;
    float bufferSamplesPerCH2WfCycle;

	isoDriver* m_virtualParent;

    void addTriggerPosition(uint32_t m_back, uint32_t position, int n_cycles);
signals:
	void fileIOinternalDisable();
public slots:
	void enableFileIO(QFile* file, int samplesToAverage, qulonglong max_file_size);
	void disableFileIO();
};

#endif // ISOBUFFER_H

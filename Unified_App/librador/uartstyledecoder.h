#ifndef UARTSTYLEDECODER_H
#define UARTSTYLEDECODER_H

#include "isobufferbuffer.h"
#include "logging_internal.h"
#include "o1buffer.h"
#include <mutex>
#include <limits.h>
#include <stdint.h>
#include <chrono>

#define SERIAL_BUFFER_LENGTH 8192

constexpr double SERIAL_DELAY = 0.01;  //100 baud?

enum class UartParity : uint8_t
{
    None,
    Even,
    Odd
};
// Character coding of the decoded symbols. Standard8Bit is the classic
// 8-data-bit UART byte stream; Baudot is 5-data-bit ITA2 telex with
// letters/figures shift tracking (ported from the Qt app's decodeDatabit
// mode-5 scaffolding, tables completed here).
enum class UartMode : uint8_t
{
    Standard8Bit,
    Baudot
};
struct UartSettings {
    bool decode_on = false;
    double baudRate = 300;
    UartParity parity = UartParity::None;
    UartMode mode = UartMode::Standard8Bit;
};

class uartStyleDecoder
{
public:
    explicit uartStyleDecoder(o1buffer *parent);
private:
    o1buffer *m_parent;

	// Indicates the current bit being decoded.
    int serialPtr_bit;

    bool allZeroes = false;
    bool uartTransmitting = false;
    bool newUartSymbol = false;
    uint32_t dataBit_current = 0;
    uint32_t parityIndex = UINT_MAX;
    uint32_t dataBit_max = 8;
    unsigned short currentUartSymbol = 0;
    bool jitterCompensationNeeded = true;

    void updateSerialPtr(bool current_bit);
    bool getNextUartBit() const;
    void decodeNextUartBit(bool bitValue);
    bool jitterCompensationProcedure(bool current_bit);

    bool m_hexDisplay = false;
    bool escape_code_started = false;

    // ITA2 (Baudot) shift state: false = letters (LTRS), true = figures (FIGS).
    // Telex lines start out in letters shift.
    bool baudotFigureShift = false;

    char convertedStream_string[SERIAL_BUFFER_LENGTH + 1];
    isoBufferBuffer m_serialBuffer;

    bool switched_on = false;
public:

//     QTimer m_updateTimer; // IMPORTANT: must be after m_serialBuffer. construction / destruction order matters
    void UartDecode();
    int serialDistance() const;

    void wireDisconnected(int ch);

    void updateConsole();

    void setSettings(UartSettings new_settings);

    char * getString(bool* parity_check);
private:
    // Non-const: Baudot decoding mutates the LTRS/FIGS shift state.
    char decodeDatabit(int mode, short symbol);
    char decodeBaudot(short symbol);

	std::mutex mutex;

    bool isParityCorrect(unsigned short bitField, bool bitValue) const;
	UartParity parityOf(unsigned short bitField) const;

    bool parityCheckFailed = false;

    UartSettings m_settings;
};

#endif // UARTSTYLEDECODER_H

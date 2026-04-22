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
struct UartSettings {
    bool decode_on = false;
    double baudRate = 300;
    UartParity parity = UartParity::None;
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
    char decodeDatabit(int mode, short symbol) const;
    char decodeBaudot(short symbol) const;

	std::mutex mutex;

    bool isParityCorrect(unsigned short bitField, bool bitValue) const;
	UartParity parityOf(unsigned short bitField) const;

    bool parityCheckFailed = false;

    UartSettings m_settings;
};

#endif // UARTSTYLEDECODER_H

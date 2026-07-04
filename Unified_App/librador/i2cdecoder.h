#ifndef I2CDECODER_H
#define I2CDECODER_H

#include "o1buffer.h"
#include "isobufferbuffer.h"
#include "logging_internal.h"

#include <mutex>

#define SERIAL_BUFFER_LENGTH 8192


namespace i2c
{

enum class transmissionState: uint8_t
{
	unknown,
	idle,
	address,
	data	
};

enum class edge: uint8_t
{
	rising,
	falling,
	held_high,
	held_low
};

constexpr uint8_t addressBitStreamLength = 9;
constexpr uint8_t dataBitStreamLength = 9;
constexpr uint32_t I2C_BUFFER_LENGTH = 4096;
constexpr double SERIAL_DELAY = 0.01;

class i2cDecoder
{
    char convertedStream_string[SERIAL_BUFFER_LENGTH + 1];

public:
    explicit i2cDecoder(o1buffer* sda_in, o1buffer* scl_in);
	// misc
    o1buffer* sda;
	o1buffer* scl;
    isoBufferBuffer m_serialBuffer;
    std::mutex mutex;

	// State vars
	uint8_t currentSdaValue = 0;
	uint8_t previousSdaValue = 0;
	uint8_t currentSclValue = 0;
	uint8_t previousSclValue = 0;
    uint64_t serialPtr_bit = 0;
	transmissionState state = transmissionState::unknown;
    bool consoleStateInvalid;

	// Data Transmission
	uint8_t currentBitIndex = 0;
    uint16_t currentBitStream;

	// Member functions
	void updateBitValues();
	void runStateMachine();
    void run(); 
    int serialDistance(o1buffer* buffer);
	edge edgeDetection(uint8_t current, uint8_t prev);
	void decodeAddress(edge sdaEdge, edge sclEdge);
	void decodeData(edge sdaEdge, edge sclEdge);
	void startCondition();
	void stopCondition();
    void reset();

    void setIsDecoding(bool new_decode_on);
    char * getString();

    bool m_decode_on = false;
};

} // Namespace i2c

#endif // UARTSTYLEDECODER_H

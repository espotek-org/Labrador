#include "i2cdecoder.h"

#include <cstring>
#include <stdexcept>

using namespace i2c;

i2cDecoder::i2cDecoder(o1buffer* sda_in, o1buffer* scl_in) : 
    m_serialBuffer{I2C_BUFFER_LENGTH},
    sda(sda_in),
    scl(scl_in)
{
}

void i2cDecoder::reset()
{
    LIBRADOR_LOG(LOG_DEBUG, "Resetting I2C");

    if (sda->mostRecentAddress != scl->mostRecentAddress)
    {
        // Perhaps the data could be saved, but just resetting them seems much safer
        sda->reset(true);
        scl->reset(true);
    }

    serialPtr_bit = sda->mostRecentAddress * 8;

    {
        std::lock_guard<std::mutex> lock(mutex);

        m_serialBuffer.clear();
    }
}


void i2cDecoder::run()
{
    if(!m_decode_on)
        return;

    while (serialDistance(sda) > SERIAL_DELAY * sda->m_samples_per_second * 8)
	{
		updateBitValues();
		runStateMachine();
		serialPtr_bit ++;
        if (serialPtr_bit >= (sda->m_bufferLen * 8))
            serialPtr_bit -= (sda->m_bufferLen * 8);
	}	
} 

int i2cDecoder::serialDistance(o1buffer* buffer)
{
    int back_bit = buffer->mostRecentAddress * 8;
    int bufferEnd_bit = buffer->m_bufferLen * 8;
    if (back_bit >= serialPtr_bit)
        return back_bit - serialPtr_bit;
    else
		return bufferEnd_bit - serialPtr_bit + back_bit;
}

void i2cDecoder::updateBitValues(){
    previousSdaValue = currentSdaValue;
    previousSclValue = currentSclValue;

    int coord_byte = serialPtr_bit/8;
    int coord_bit = serialPtr_bit - (8*coord_byte);
    unsigned char dataByteSda = sda->get(coord_byte);
    unsigned char dataByteScl = scl->get(coord_byte);
    unsigned char mask = (0x01 << coord_bit);
    currentSdaValue = dataByteSda & mask;
	currentSclValue = dataByteScl & mask;
}

void i2cDecoder::runStateMachine()
{
    edge sdaEdge = edgeDetection(currentSdaValue, previousSdaValue);
	edge sclEdge = edgeDetection(currentSclValue, previousSclValue);

	if ((sdaEdge == edge::rising) && (sclEdge == edge::falling)) // INVALID STATE TRANSITION
	{
        state = transmissionState::unknown;
        LIBRADOR_LOG(LOG_WARNING, "Dumping I2C state and aborting...");
        for (int i=31; i>=0; i--)
            LIBRADOR_LOG(LOG_DEBUG, "%02x\t%02x", sda->get(serialPtr_bit/8 - i) & 0xFF, scl->get(serialPtr_bit/8 - i) & 0xFF);
        throw std::runtime_error("unknown i2c transmission state");
        return;
	}

	if ((sdaEdge == edge::rising) && (sclEdge == edge::held_high)) // START
	{
        stopCondition();
		return;
	}

	if ((sdaEdge == edge::falling) && (sclEdge == edge::held_high)) // STOP
	{
        startCondition();
		return;
	}

	switch (state)
	{
		case transmissionState::idle:
			return;
		case transmissionState::address:
			decodeAddress(sdaEdge, sclEdge);
			break;
		case transmissionState::data:
			decodeData(sdaEdge, sclEdge);
			break;		
	}
}

edge i2cDecoder::edgeDetection(uint8_t current, uint8_t prev)
{
	if (current && prev)
		return edge::held_high;
	if (!current && !prev)
		return edge::held_low;
	if (current && !prev)
		return edge::rising;
    if (!current && prev)
		return edge::falling;

    throw std::runtime_error("i2c Edge Detection critical failure");
}

void i2cDecoder::decodeAddress(edge sdaEdge, edge sclEdge)
{
	// Read in the next bit.
    if (sclEdge == edge::rising && sdaEdge == edge::held_high && currentBitIndex++ < addressBitStreamLength)
          currentBitStream = (currentBitStream << 1) | 0x0001;
    else if (sclEdge == edge::rising && sdaEdge == edge::held_low && currentBitIndex++ < addressBitStreamLength)
        currentBitStream = (currentBitStream << 1) & 0xFFFE;
    else
        return;

    if (currentBitIndex == addressBitStreamLength)
    {
        LIBRADOR_LOG(LOG_DEBUG, "Finished Address Decode");
        if (currentBitStream & 0b0000000000000010)
            m_serialBuffer.insert("READ:  ");
        else
            m_serialBuffer.insert("WRITE: ");

        m_serialBuffer.insert_hex((uint8_t)((currentBitStream & 0b0000000111111100) >> 2));
        m_serialBuffer.insert(' ');

        if (currentBitStream & 0b0000000000000001)
            m_serialBuffer.insert("(NACK)");

        consoleStateInvalid = true;

        // Prepare for next bit
        currentBitIndex = 0;
        currentBitStream = 0x0000;
        state = transmissionState::data;
    }
}

void i2cDecoder::decodeData(edge sdaEdge, edge sclEdge)
{
    // Read in the next bit.
    if (sclEdge == edge::rising && sdaEdge == edge::held_high && currentBitIndex++ < dataBitStreamLength)
          currentBitStream = (currentBitStream << 1) | 0x0001;
    else if (sclEdge == edge::rising && sdaEdge == edge::held_low && currentBitIndex++ < dataBitStreamLength)
        currentBitStream = (currentBitStream << 1) & 0xFFFE;
    else
        return;

    if (currentBitIndex == dataBitStreamLength)
    {
        LIBRADOR_LOG(LOG_DEBUG, "Finished Data byte Decode");

        m_serialBuffer.insert_hex((uint8_t)((currentBitStream & 0b0000000111111110) >> 1));
        m_serialBuffer.insert(' ');

        if (currentBitStream & 0b0000000000000001)
            m_serialBuffer.insert("(NACK)");

        consoleStateInvalid = true;

        // Prepare for next bit
        currentBitIndex = 0;
        currentBitStream = 0x0000;
    }
}

void i2cDecoder::startCondition()
{
	currentBitIndex = 0;
    currentBitStream = 0x0000;
	state = transmissionState::address;	
    LIBRADOR_LOG(LOG_DEBUG, "I2C START");
}

void i2cDecoder::stopCondition()
{
    state = transmissionState::idle;
    m_serialBuffer.insert('\n');
    LIBRADOR_LOG(LOG_DEBUG, "I2C STOP");
}

char * i2cDecoder::getString()
{
    memcpy(convertedStream_string, m_serialBuffer.begin(), sizeof(char) * m_serialBuffer.size());
    convertedStream_string[m_serialBuffer.size()] = '\0';
    return convertedStream_string;
}

void i2cDecoder::setIsDecoding(bool new_decode_on)
{
    if(new_decode_on && !m_decode_on)
        m_serialBuffer.clear();
        reset();
    m_decode_on = new_decode_on;
}

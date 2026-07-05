#include "uartstyledecoder.h"
#include <cassert>
#include <cstring>

uartStyleDecoder::uartStyleDecoder(o1buffer *parent) :
    m_serialBuffer{SERIAL_BUFFER_LENGTH},
    m_parent(parent)
{
}


void uartStyleDecoder::UartDecode()
{
    if(!m_settings.decode_on)
        return;
    if(switched_on) {
        m_serialBuffer.clear();
	// Begin decoding SAMPLE_DELAY seconds in the past.
        serialPtr_bit = (int)(m_parent->mostRecentAddress * 8 - SERIAL_DELAY * m_parent->m_samples_per_second * 8 + m_parent->m_bufferLen * 8) % (m_parent->m_bufferLen*8);
        switched_on = false;
    }
    double dist_seconds = (double)serialDistance()/(8 * m_parent->m_samples_per_second);
    double bitPeriod_seconds = 1.0 / m_settings.baudRate;

    // Used to check for wire disconnects.  You should get at least one "1" for a stop bit.
    allZeroes = true;

    while(dist_seconds > (bitPeriod_seconds + SERIAL_DELAY))
	{
        // Read next uart bit
        bool uart_bit = getNextUartBit();

        if (uart_bit == 1)
            allZeroes = false;

        // Process it
        if (uartTransmitting)
        {
            decodeNextUartBit(uart_bit);
        }
        else
        {
			// Uart starts transmitting after start bit (logic low).
            uartTransmitting = uart_bit == false;
            jitterCompensationNeeded = true;
        }

        // Update the pointer, accounting for jitter
        updateSerialPtr(uart_bit);
        // Calculate stopping condition
        dist_seconds = (double)serialDistance()/(8*m_parent->m_samples_per_second);
    }

    //Not a single stop bit, or idle bit, in the whole stream.  Wire must be disconnected.
    if (allZeroes)
	{
        LIBRADOR_LOG(LOG_WARNING, "Wire Disconnect detected!");
    }
}

int uartStyleDecoder::serialDistance() const
{
    int back_bit = m_parent->mostRecentAddress * 8;
    int bufferEnd_bit = (m_parent->m_bufferLen-1) * 8;
    if (back_bit >= serialPtr_bit)
        return back_bit - serialPtr_bit;
    else
		return bufferEnd_bit - serialPtr_bit + back_bit;
}

void uartStyleDecoder::updateSerialPtr(bool current_bit)
{
    if (jitterCompensationNeeded && uartTransmitting)
        jitterCompensationNeeded = jitterCompensationProcedure(current_bit);

    int distance_between_bits = (8*m_parent->m_samples_per_second)/ m_settings.baudRate;
    if (uartTransmitting)
        serialPtr_bit += distance_between_bits;
	else
		serialPtr_bit += (distance_between_bits - 1);  //Less than one baud period so that it will always see that start bit.

    if (serialPtr_bit >= (m_parent->m_bufferLen * 8))
        serialPtr_bit -= (m_parent->m_bufferLen * 8);
}

bool uartStyleDecoder::getNextUartBit() const
{
	int bitIndex = serialPtr_bit;

    int coord_byte = bitIndex/8;
    int coord_bit = bitIndex - (8*coord_byte);
    uint8_t dataByte = m_parent->get(coord_byte);
    uint8_t mask = (0x01 << coord_bit);
    return dataByte & mask;
}

void uartStyleDecoder::decodeNextUartBit(bool bitValue)
{
    if (dataBit_current == parityIndex)
    {
        parityCheckFailed = !isParityCorrect(currentUartSymbol, bitValue);
        dataBit_current++;
    }
    else if (dataBit_current < dataBit_max)
    {
        currentUartSymbol |= (bitValue << dataBit_current);
        dataBit_current++;
    }
    else
    {
        char decodedDatabit = decodeDatabit(dataBit_max, currentUartSymbol);

// 		if (parityCheckFailed)
// 		{
// 			m_serialBuffer.insert("\n<ERROR: Following character contains parity error>\n");
// 		}

        // Start + body of escape code
        if(decodedDatabit == 0x1b || (escape_code_started && !((decodedDatabit >= 'A' && decodedDatabit <= 'Z') || (decodedDatabit >= 'a' && decodedDatabit <= 'z'))))
        {
            escape_code_started = true;
        }
        // End of escape code
        else if(escape_code_started && ((decodedDatabit >= 'A' && decodedDatabit <= 'Z') || (decodedDatabit >= 'a' && decodedDatabit <= 'z')))
        {
            escape_code_started = false;
        }
        else
        {
            if (m_hexDisplay)
            {
                m_serialBuffer.insert_hex(decodedDatabit);
                m_serialBuffer.insert(" ");
            }
            else
            {
                if(decodedDatabit=='\0') {
                    // In Baudot mode '\0' means "nothing to print" (ITA2 blank
                    // or a LTRS/FIGS shift code) — emit nothing.
                    if(!allZeroes && m_settings.mode == UartMode::Standard8Bit)
                        m_serialBuffer.insert("\\0"); //insert escaped null terminator in place of null terminator
                } else {
                    m_serialBuffer.insert(decodedDatabit);
                }
            }
        }

        currentUartSymbol = 0;
        dataBit_current = 0;
        uartTransmitting = false;
    }
}

//This function compensates for jitter by, when the current bit is a "1", and the last bit was a zero, setting the pointer
//to the sample at the midpoint between this bit and the last.
bool uartStyleDecoder::jitterCompensationProcedure(bool current_bit)
{

    //We only run when the current bit is a "1", to prevent slowdown when there are long breaks between transmissions.
    if (current_bit == false)
        return true;

    //Can't be bothered dealing with the corner case where the serial pointer is at the very start of the buffer.
    //Just return and try again next time.
    int left_coord = serialPtr_bit - (8*m_parent->m_samples_per_second)/ m_settings.baudRate;
    LIBRADOR_LOG(LOG_DEBUG, "left_coord = %d", left_coord);
    if (left_coord < 0)
        return true; //Don't want to read out of bounds!!

    //The usual case, when transmitting anyway.
    uint8_t left_byte = (m_parent->get(left_coord/8) & 0xff);
    //Only run when a zero is detected in the leftmost symbol.
    if (left_byte != 0xff)
	{
        //Step back, one sample at a time, to the 0->1 transition point
        bool temp_bit = 1;
        while(temp_bit)
		{
            temp_bit = getNextUartBit();
            serialPtr_bit--;
        }
        //Jump the pointer forward by half a uart bit period, and return "done!".
        serialPtr_bit += (8*m_parent->m_samples_per_second/ m_settings.baudRate)/2;
        return false;
    }
    return true;
}

// Character-map dispatch on the number of data bits, restored from the Qt
// app (Desktop_Interface/uartstyledecoder.cpp): "Basically scaffolding to add
// character maps for other modes (5 bit, for example)."
char uartStyleDecoder::decodeDatabit(int mode, short symbol)
{
    switch(mode)
    {
        case 5:  //5-bit ITA2 (Baudot)
            return decodeBaudot(symbol);
        case 8:  //8-bit ASCII;
            return symbol;
        default:
            LIBRADOR_LOG(LOG_WARNING, "uartStyleDecoder::decodeDatabit is failing...");
            return -1; // Garbage
    }
}

// ITA2 shift codes (5-bit values; bits are collected LSB-first, so the
// first-transmitted ITA2 bit 1 is bit 0 of the symbol).
static constexpr short BAUDOT_FIGS = 0x1B;
static constexpr short BAUDOT_LTRS = 0x1F;

char uartStyleDecoder::decodeBaudot(short symbol)
{
    // ITA2 (International Telegraph Alphabet No. 2) tables, indexed by the
    // 5-bit symbol with the first-transmitted bit as the LSB. Positions 27
    // (FIGS) and 31 (LTRS) are handled before lookup. 0x05 is ENQ ("WRU?"),
    // 0x07 is BEL; the national-use figures for F, G and H follow the common
    // '!', '&', '#' assignments.
    static constexpr char letters[32] = {
        '\0', 'E',  '\n', 'A',  ' ',  'S',  'I',  'U',
        '\r', 'D',  'R',  'J',  'N',  'F',  'C',  'K',
        'T',  'Z',  'L',  'W',  'H',  'Y',  'P',  'Q',
        'O',  'B',  'G',  '\0', 'M',  'X',  'V',  '\0'
    };
    static constexpr char figures[32] = {
        '\0', '3',  '\n', '-',  ' ',  '\'', '8',  '7',
        '\r', 0x05, '4',  0x07, ',',  '!',  ':',  '(',
        '5',  '+',  ')',  '2',  '#',  '6',  '0',  '1',
        '9',  '?',  '&',  '\0', '.',  '/',  '=',  '\0'
    };

    short index = symbol & 0x1F;
    if (index == BAUDOT_FIGS)
    {
        baudotFigureShift = true;
        return '\0';
    }
    if (index == BAUDOT_LTRS)
    {
        baudotFigureShift = false;
        return '\0';
    }
    return baudotFigureShift ? figures[index] : letters[index];
}

bool uartStyleDecoder::isParityCorrect(unsigned short bitField, bool bitValue) const
{
	assert(m_settings.parity != UartParity::None);
    bitField |= (bitValue << parityIndex);
	
	return parityOf(bitField) == m_settings.parity;
}

UartParity uartStyleDecoder::parityOf(unsigned short bitField) const
{
	bool result = false;

	for (uint32_t mask = 1 << dataBit_max; mask != 0; mask >>= 1)
		result ^= static_cast<bool>(bitField & mask);

	return result ? UartParity::Odd : UartParity::Even;
}

void uartStyleDecoder::setSettings(UartSettings new_settings)
{
    if(new_settings.decode_on && !m_settings.decode_on) {
        switched_on = true;
        baudotFigureShift = false; // ITA2 lines start in letters shift
    } else if (!new_settings.decode_on) {
        switched_on = false;
        uartTransmitting = false;
        escape_code_started = false;
    }

    if (new_settings.mode != m_settings.mode) {
        // A character-set change invalidates any half-assembled symbol and
        // the Baudot shift state.
        currentUartSymbol = 0;
        dataBit_current = 0;
        uartTransmitting = false;
        baudotFigureShift = false;
    }

    // Standard = 8 data bits, Baudot (ITA2) = 5 data bits.
    dataBit_max = (new_settings.mode == UartMode::Baudot) ? 5 : 8;

    switch(new_settings.parity)
    {
    case UartParity::None:
        parityIndex = UINT_MAX;
        break;
    case UartParity::Even:
    case UartParity::Odd:
        parityIndex = dataBit_max;
    }
    m_settings = new_settings;
}

char * uartStyleDecoder::getString(bool* parity_check)
{
	if(m_settings.parity == UartParity::None) {
        *parity_check = true;
    } else {
        *parity_check = !parityCheckFailed;
    }
    memcpy(convertedStream_string, m_serialBuffer.begin(), sizeof(char) * m_serialBuffer.size());
    convertedStream_string[m_serialBuffer.size()] = '\0';
    return convertedStream_string;
}

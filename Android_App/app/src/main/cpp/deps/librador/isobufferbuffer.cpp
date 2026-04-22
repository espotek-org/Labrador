#include "isobufferbuffer.h"
#include <android/log.h>
#define LOG_TAG "librador"
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__)

/* isoBufferBuffer is implemented as two consecutive, duplicate,
 * ring buffers. the effect of this is that we are able to hand
 * out pointers to pieces of contiguous memory representing the
 * last N inserted elements (for some N <= capacity()) even
 * when we looped back to the begining of the ring buffer less
 * than N insertions ago
 *
 *     There are some differences with the original implementation
 * functionality-wise.
 *     Where the original implementation allowed queries half as
 * long as the length passed in, and allocated a buffer 1.5 times
 * the length passed in, this version allows queries of length up
 * to the length passed into the constructor and allocates a
 * buffer 2 times the length passed in.
 *     Overall, this means that in contrast to the original
 * implementation which only allowed queries up to a third as
 * long as the allocated buffer, we can now do queries as long as
 * half of the allocated buffer, which is a notable improvement.
 */

isoBufferBuffer::isoBufferBuffer(uint32_t length)
	: m_data(std::make_unique<char[]>(length*2))
	, m_capacity(length)
{
}

// Adds a character to the end of the buffer
void isoBufferBuffer::insert(char c)
{
	char* dataPtr = m_data.get();

	// Add character to first half of the buffer
	dataPtr[m_top] = c;
	// Then to the second
	dataPtr[m_top+m_capacity] = c;

	// Loop the buffer index if necessary and update size accordingly
	m_top = (m_top + 1) % m_capacity;
	m_size = std::min(m_size + 1, m_capacity);
}

void isoBufferBuffer::insert(char const * s)
{
	while (*s != '\0')
		insert(*s++);
}

void isoBufferBuffer::insert(std::string const & s)
{
	for (char c : s)
		insert(c);
}

void isoBufferBuffer::insert_hex(uint8_t x)
{
	char str[5];
	snprintf(str, sizeof str, "0x%02hhx", x);
	insert((char const *)str);
}

char const* isoBufferBuffer::query(uint32_t count) const
{
	if (count > m_capacity)
		LOGF("isoBufferBuffer::query : you may not request more items than the capacity of the buffer");

	if (count > m_size)
		LOGF("isoBufferBuffer::query : you may not request more items than inserted");

	return end() - count;
}

void isoBufferBuffer::clear()
{
	m_top = 0;
	m_size = 0;
}

char const * isoBufferBuffer::begin() const
{
	return m_data.get() + m_top - m_size + m_capacity;
}

char const * isoBufferBuffer::end() const
{
	return m_data.get() + m_top + m_capacity;
}

uint32_t isoBufferBuffer::size() const
{
	return m_size;
}

uint32_t isoBufferBuffer::capacity() const
{
	return m_capacity;
}

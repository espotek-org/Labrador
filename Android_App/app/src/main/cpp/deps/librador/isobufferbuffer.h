#ifndef ISOBUFFERBUFFER_H
#define ISOBUFFERBUFFER_H

#include <cstdint>
#include <string>
#include <memory>

/** @file isobufferbuffer.h
 *  @brief This  module  implements  a  data structure that allows
 *  insertion  of  single  characters  and  a  view  of the last N
 *  inserted characters in constant time.
 *
 *  To  obtain  such  complexity,  a  double  ring buffer is used.
 *  That  is,  two  identical  ring buffers are mantained adjacent
 *  in memory. If we always return a pointer to the beginning of a
 *  range  that ends in the second buffer, we will always return a
 *  valid  address(*),  even  when the requested length is greater
 *  than  the  current position being inserted into in the buffer.
 *
 *  (*) By  valid  address  I  mean  that  both the addresses that
 *  represent  the beginning and end of the requested query result
 *  are within the allocated buffer.
 */
class isoBufferBuffer
{
public:
	isoBufferBuffer(uint32_t length);
	~isoBufferBuffer() = default;

	void insert(char c);
	void insert(char const * s);
	void insert(std::string const & s);
	void insert_hex(uint8_t x);

	char const * query(uint32_t length) const;
	// TODO?: add ability to get a copy of the content
	// (e.g. return std::string or Qstring)

	void clear();

	char const * begin() const;
	char const * end() const;

	uint32_t size() const;
	uint32_t capacity() const;
private:
	std::unique_ptr<char[]> m_data;
	uint32_t m_capacity;
	uint32_t m_size = 0;
	uint32_t m_top = 0;
};

#endif // ISOBUFFERBUFFER_H

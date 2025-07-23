#pragma once

#include <cstdint>
#include <vector>





namespace ByteStream
{





/** Adds 2 bytes into the output, containing the specified 16-bit value (Little-endian). */
inline void writeUint16(std::vector<char> & aOutput, uint16_t aValue)
{
	aOutput.push_back(static_cast<char>(aValue        & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 8) & 0xff));
}





/** Adds 4 bytes into the output, containing the specified 32-bit value (Little-endian). */
inline void writeUint32(std::vector<char> & aOutput, uint32_t aValue)
{
	aOutput.push_back(static_cast<char>(aValue         & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 8)  & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 16) & 0xff));
	aOutput.push_back(static_cast<char>((aValue >> 24) & 0xff));
}





/** Parses 2 bytes from the input as a 16-bit value. */
inline uint16_t parseUint16(const uint8_t * aInput)
{
	return aInput[0] | (aInput[1] << 8);
}





/** Parses 2 bytes from the input as a 16-bit value. */
inline uint16_t parseUint16(const char * aInput)
{
	return parseUint16(reinterpret_cast<const uint8_t *>(aInput));
}





/** Parses 4 bytes from the input as a 32-bit value. */
inline uint32_t parseUint32(const uint8_t * aInput)
{
	return aInput[0] | (aInput[1] << 8) | (aInput[2] << 16) | (aInput[3] << 24);
}





/** Parses 4 bytes from the input as a 32-bit value. */
inline uint32_t parseUint32(const char * aInput)
{
	return parseUint32(reinterpret_cast<const uint8_t *>(aInput));
}





};  // namespace ByteStream

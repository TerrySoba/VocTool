#ifndef DECODE_CREATIVE_ADPCM_H
#define DECODE_CREATIVE_ADPCM_H

#include <vector>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace {

/**
 * This class is a decoder for 4bit Creative ADPCM
 * 
 * Sources:
 *  https://github.com/schlae/sb-firmware/blob/master/sbv202.asm
 *  https://github.com/joncampbell123/dosbox-x/blob/master/src/hardware/sblaster.cpp
 *  https://wiki.multimedia.cx/index.php/Creative_8_bits_ADPCM
 */
class CreativeAdpcmDecoder4Bit
{
public:
    CreativeAdpcmDecoder4Bit(uint8_t firstValue) :
        m_accumulator(1),                           // initialize accumulator to 1
        m_previous(firstValue)
    {}

    /**
     * Decode a nibble (4 bits) of data and return the 8bit data.
     * 
     * @param nibble The 4bits to be decoded. Value must be smaller than 16.
     */
    uint8_t decodeNibble(uint8_t nibble)
    {
        int sign = (nibble & 8) / 4 - 1;            // Input is just 4 bits (a nibble), so the 4th bit is the sign bit
        uint8_t data = nibble & 7;                  // The lower 3 bits are the sample data
        uint8_t delta = 
            (data * m_accumulator) +
            (m_accumulator / 2);                    // Scale sample data using accumulator value
        int result = m_previous + (sign * delta);   // Calculate the next value
        m_previous = std::clamp(result, 0, 255);    // Limit value to 0..255

        if ((data == 0) && (m_accumulator > 1))     // If input value is 0, and accumulator is
            m_accumulator /= 2;                     // larger than 1, then halve accumulator.
        if ((data >= 5) && (m_accumulator < 8))     // If input value larger than 5, and accumulator is
            m_accumulator *= 2;                     // lower than 8, then double accumulator.

        return m_previous;
    }

private:
    uint8_t m_accumulator;
    uint8_t m_previous;
};



std::vector<uint8_t> breakBytes(const std::vector<uint8_t>& data, uint8_t bitsPerNibble)
{
    uint32_t nibbleCount = 0;
    if (bitsPerNibble == 4)
    {
        nibbleCount = data.size() * 2;
    }
    else if (bitsPerNibble == 2)
    {
        nibbleCount = data.size() * 4;
    }
    else
    {
        throw std::runtime_error("Unsupported bits per nibble");
    }
    
    std::vector<uint8_t> nibbles;
    nibbles.reserve(nibbleCount);

    for (uint8_t byte : data)
    {
        if (bitsPerNibble == 4)
        {
            nibbles.push_back(byte >> 4);
            nibbles.push_back(byte & 0x0F);
        }
        else if (bitsPerNibble == 2)
        {
            nibbles.push_back((byte >> 6) & 0x03);
            nibbles.push_back((byte >> 4) & 0x03);
            nibbles.push_back((byte >> 2) & 0x03);
            nibbles.push_back((byte >> 0) & 0x03);
        }
    }

    return nibbles;
}


std::vector<uint8_t> decodeAdpcm4(uint8_t initial, std::vector<uint8_t> data)
{
    auto nibbles = breakBytes(data, 4);

    CreativeAdpcmDecoder4Bit decoder(initial);

    std::vector<uint8_t> decoded(nibbles.size() + 1);
    decoded[0] = initial;

    for (size_t i = 0; i < nibbles.size(); ++i)
    {
        decoded[i + 1] = decoder.decodeNibble(nibbles[i]);
    }

    return decoded;
}


class CreativeAdpcmDecoder2Bit
{
public:
    CreativeAdpcmDecoder2Bit(uint8_t firstValue) :
        m_scale(0),          
        m_previous(firstValue)
    {}

    // imported from https://github.com/joncampbell123/dosbox-x/blob/master/src/hardware/sblaster.cpp
    uint8_t decode2bits(uint8_t sample)
    {
        static const int8_t scaleMap[24] = {
            0, 1, 0, -1, 1, 3, -1, -3,
            2, 6, -2, -6, 4, 12, -4, -12,
            8, 24, -8, -24, 16, 48, -16, -48};
        static const uint8_t adjustMap[24] = {
            0, 4, 0, 4,
            252, 4, 252, 4, 252, 4, 252, 4,
            252, 4, 252, 4, 252, 4, 252, 4,
            252, 0, 252, 0};

        int32_t samp = sample + m_scale;
        if ((samp < 0) || (samp > 23))
        {
            if (samp < 0)
                samp = 0;
            if (samp > 23)
                samp = 23;
        }

        int32_t ref = m_previous + scaleMap[samp];
        if (ref > 0xff)
            m_previous = 0xff;
        else if (ref < 0x00)
            m_previous = 0x00;
        else
            m_previous = (uint8_t)(ref & 0xff);
        m_scale = (m_scale + adjustMap[samp]) & 0xff;

        return m_previous;
    }

private:
    uint32_t m_scale;
    uint8_t m_previous;
};


std::vector<uint8_t> decodeAdpcm2(uint8_t initial, std::vector<uint8_t> data)
{
    auto nibbles = breakBytes(data, 2);

    CreativeAdpcmDecoder2Bit decoder(initial);

    std::vector<uint8_t> decoded(nibbles.size() + 1);
    decoded[0] = initial;

    for (size_t i = 0; i < nibbles.size(); ++i)
    {
        decoded[i + 1] = decoder.decode2bits(nibbles[i]);
    }

    return decoded;
}

}

#endif

#include "voc_format.h"
#include "decode_creative_adpcm.h"

#include <string>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <iostream>

void append(std::vector<uint8_t>& container, const std::string& value)
{
    container.insert(container.end(), value.c_str(), value.c_str() + value.size());
}

void append(std::vector<uint8_t>& container, uint16_t value)
{
    container.push_back(value & 0xff);
    container.push_back((value >> 8) & 0xff);
}

void append(std::vector<uint8_t>& container, uint8_t value)
{
    container.push_back(value);
}

std::vector<uint8_t> createVocFile(
    uint32_t frequency,
    const std::vector<uint8_t>& sampleData,
    VocSampleFormat sampleFormat)
{
    
    // minimum frequency for VOC files
    // This is limited by the way VOC files encode the time constant
    const uint32_t MIN_VOC_FREQUENCY = 3908;
    if (frequency < MIN_VOC_FREQUENCY)
    {
        throw std::runtime_error("Frequency too low for VOC file. Minimum frequency is 3908 Hz.");
    }

    uint8_t timeConstant = static_cast<uint8_t>(round(256 - 1000000.0 / frequency));

    std::string vocHeader = "Creative Voice File\x1a";

    uint16_t major = 1;
    uint16_t minor = 10;
    uint16_t version = minor + (major << 8);
    uint16_t versionCheck = (~version + 0x1234);

    std::vector<uint8_t> out;

    append(out, vocHeader);
    append(out, (uint16_t)0x1a);
    append(out, version);
    append(out, versionCheck);
    append(out, (uint8_t)1); // sample header

    // now append size (3 bytes)
    uint32_t sampleSize = static_cast<uint32_t>(sampleData.size() + 2);
    out.push_back(sampleSize & 0xff);
    out.push_back(sampleSize >> 8 & 0xff);
    out.push_back(sampleSize >> 16 & 0xff);

    append(out, timeConstant);
    append(out, (uint8_t)sampleFormat);

    out.insert(out.end(), sampleData.begin(), sampleData.end());

    append(out, (uint8_t)0); // end marker

    return out;
}

void safeRead(void* buffer, size_t size, FILE* fp)
{
    if (fread(buffer, 1, size, fp) != size)
    {
        throw std::runtime_error("Could not read from file");
    }
}

uint32_t timeConstantToFrequency(uint8_t timeConstant)
{
    return 1000000 / (256 - timeConstant);
}

VocFile readVocFile(const std::string &filename)
{
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp)
    {
        throw std::runtime_error("Could not open file: " +  filename);
    }

    std::string vocHeader = "Creative Voice File\x1a";
    uint8_t buffer[128];
    safeRead(buffer, vocHeader.size(), fp);

    if (memcmp(buffer, vocHeader.c_str(), vocHeader.size()) != 0)
    {
        throw std::runtime_error("Invalid VOC file. Header does not match.");
    }

    uint16_t dummy;
    safeRead(&dummy, 2, fp);

    uint16_t version;
    safeRead(&version, 2, fp);

    uint16_t versionCheck;
    safeRead(&versionCheck, 2, fp);

    if ((~version + 0x1234) != versionCheck)
    {
        throw std::runtime_error("Invalid VOC file. Version check failed.");
    }

    uint8_t headerType;
    safeRead(&headerType, 1, fp);

    if (headerType != 1)
    {
        throw std::runtime_error("Invalid VOC file. Header type is not 1.");
    }

    uint8_t sampleA, sampleB, sampleC;
    safeRead(&sampleA, 1, fp);
    safeRead(&sampleB, 1, fp);
    safeRead(&sampleC, 1, fp);
    uint32_t sampleSize = sampleA | (sampleB << 8) | (sampleC << 16);

    uint8_t timeConstant;
    safeRead(&timeConstant, 1, fp);

    uint8_t sampleFormat;
    safeRead(&sampleFormat, 1, fp);

    std::vector<uint8_t> sampleData(sampleSize - 2);
    safeRead(sampleData.data(), sampleSize - 2, fp);

    VocFile result = {
        timeConstant,
        (uint8_t)((version >> 8) & 0xff),
        (uint8_t)(version & 0xff),
        (VocSampleFormat)sampleFormat,
        sampleData};
    return result;
}

VocFile decodeToPcm(const VocFile& compressed)
{
    VocFile result = compressed;
    switch(compressed.sampleFormat)
    {
        case VocSampleFormat::VOC_FORMAT_ADPCM_2BIT:
        {
            uint8_t initial = result.sampleData[0];
            result.sampleData.erase(result.sampleData.begin()); 
            result.sampleData = decodeAdpcm2(initial, result.sampleData);
            result.sampleFormat = VocSampleFormat::VOC_FORMAT_PCM_8BIT;
            break;
        }
        case VocSampleFormat::VOC_FORMAT_ADPCM_4BIT:
        {
            uint8_t initial = result.sampleData[0];
            result.sampleData.erase(result.sampleData.begin()); 
            result.sampleData = decodeAdpcm4(initial, result.sampleData);
            result.sampleFormat = VocSampleFormat::VOC_FORMAT_PCM_8BIT;
            break;
        }
        case VocSampleFormat::VOC_FORMAT_PCM_8BIT:
        {
            // nothing, already PCM
            break;
        }
        default:
        {
            throw std::runtime_error("Unsupported sample format");
        }

    }

    return result;
}


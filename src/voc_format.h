#ifndef VOC_FORMAT_H
#define VOC_FORMAT_H

#include <vector>
#include <cstdint>
#include <string>

enum VocSampleFormat
{
    VOC_FORMAT_PCM_8BIT = 0,
    VOC_FORMAT_ADPCM_4BIT = 1,
    VOC_FORMAT_ADPCM_3BIT = 2,
    VOC_FORMAT_ADPCM_2BIT = 3,
};

std::vector<uint8_t> createVocFile(uint32_t frequency,
                                   const std::vector<uint8_t> &sampleData,
                                   VocSampleFormat sampleFormat);


struct VocFile
{
    uint8_t timeConstant;
    uint8_t majorVersion;
    uint8_t minorVersion;
    VocSampleFormat sampleFormat;
    std::vector<uint8_t> sampleData;
};

uint32_t timeConstantToFrequency(uint8_t timeConstant);
VocFile readVocFile(const std::string &filename);
VocFile decodeToPcm(const VocFile& compressed);


#endif
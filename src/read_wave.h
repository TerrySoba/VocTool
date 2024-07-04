#ifndef READ_WAVE_H
#define READ_WAVE_H

#include <vector>
#include <cstdint>
#include <array>
#include <string>

enum WaveAudioFormat : uint16_t
{
    WAVE_FORMAT_PCM = 1,
    WAVE_FORMAT_IEEE_FLOAT = 3
};

#ifdef _MSC_VER
    #pragma pack(push, 1)
#endif

struct WaveFileHeader
{
    std::array<uint8_t, 4> chunkId;
    uint32_t chunkSize;
    std::array<uint8_t, 4> format;
    std::array<uint8_t, 4> subChunk1Id;
    uint32_t subChunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t bytesPerSample;
    uint16_t bitsPerSample;

#ifdef __GNUC__
} __attribute__((packed));
#else
};
#endif

#ifdef _MSC_VER
    #pragma pack(pop)
#endif

struct WaveFile
{
    WaveFileHeader header;
    std::vector<uint8_t> rawData;
};

WaveFile loadWaveFile(const std::string& filename);

struct WaveFileMono
{
    uint32_t sampleRate;
    std::vector<double> data;
};

/**
 * @brief Loads a wave file and converts it to mono. The samples are converted to float.
 */
WaveFileMono loadWaveFileToMono(const std::string& filename);


#endif

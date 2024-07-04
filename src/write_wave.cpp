#include "write_wave.h"

#include <cstdio>
#include <stdexcept>
#include <cstring>

void write8bitMonoWaveFile(const std::string& filename, uint32_t sampleRate, const std::vector<uint8_t>& data)
{
    WaveFileHeader header;
    memcpy(header.chunkId.data(), "RIFF", 4);
    memcpy(header.format.data(), "WAVE", 4);
    memcpy(header.subChunk1Id.data(), "fmt ", 4);
    header.subChunk1Size = 16;
    header.audioFormat = WAVE_FORMAT_PCM;
    header.numChannels = 1;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 8;
    header.bytesPerSample = 1;
    header.byteRate = header.sampleRate * header.numChannels * header.bytesPerSample;
    header.chunkSize = static_cast<uint32_t>(data.size() + sizeof(WaveFileHeader));
    
    FILE* file = fopen(filename.c_str(), "wb");
    if (!file)
    {
        throw std::runtime_error("Could not open file for writing");
    }

    fwrite(&header, sizeof(WaveFileHeader), 1, file);

    uint32_t subChunk2Size = static_cast<uint32_t>(data.size());
    fwrite("data", 1, 4, file);
    fwrite(&subChunk2Size, 4, 1, file);

    fwrite(data.data(), 1, data.size(), file);
    fclose(file);
}


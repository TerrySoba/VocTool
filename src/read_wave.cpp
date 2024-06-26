#include "read_wave.h"

#include <stdexcept>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include "resampling.h"

void safeRead(void* buffer, size_t size, size_t count, FILE* file)
{
    if (fread(buffer, size, count, file) != count)
    {
        throw std::runtime_error("Failed to read file");
    }
}


WaveFileHeader readWaveFileHeader(FILE* file)
{
    WaveFileHeader header;
    safeRead(header.chunkId.data(), 4, 1, file);
    if (memcmp(header.chunkId.data(), "RIFF", 4) != 0)
    {
        throw std::runtime_error("Invalid file format, expected RIFF chunk.");
    }

    safeRead(&header.chunkSize, 4, 1, file);
    safeRead(header.format.data(), 4, 1, file);
    if (memcmp(header.format.data(), "WAVE", 4) != 0)
    {
        throw std::runtime_error("Invalid file format, expected WAVE chunk.");
    }

    safeRead(header.subChunk1Id.data(), 4, 1, file);
    if (memcmp(header.subChunk1Id.data(), "fmt ", 4) != 0)
    {
        throw std::runtime_error("Invalid file format, expected fmt");
    }

    safeRead(&header.subChunk1Size, 4, 1, file);
    safeRead(&header.audioFormat, 2, 1, file);
    safeRead(&header.numChannels, 2, 1, file);
    safeRead(&header.sampleRate, 4, 1, file);
    safeRead(&header.byteRate, 4, 1, file);
    safeRead(&header.bytesPerSample, 2, 1, file);
    safeRead(&header.bitsPerSample, 2, 1, file);

    return header;
}


std::vector<uint8_t> readWaveDataChunk(FILE* file)
{
    uint32_t subChunk2Size;
    safeRead(&subChunk2Size, 4, 1, file);

    std::vector<uint8_t> data(subChunk2Size);
    safeRead(data.data(), subChunk2Size, 1, file);

    return data;
}


std::string readChunkId(FILE* file)
{
    std::array<char, 4> chunkId;
    safeRead(chunkId.data(), 4, 1, file);
    return std::string(chunkId.data(), chunkId.size());
}


WaveFile loadWaveFile(const std::string& filename)
{
    auto file = std::shared_ptr<FILE>(
        fopen(filename.c_str(), "rb"),
        [](FILE* file) { if (file) {fclose(file);} });
    if (!file)
    {
        throw std::runtime_error("Failed to open file");
    }

    WaveFileHeader header = readWaveFileHeader(file.get());

    std::vector<uint8_t> data;

    while (!feof(file.get()))
    {
        std::string chunkId;
        try
        {
            chunkId = readChunkId(file.get());
        }
        catch (...)
        {
            break;
        }

        if (chunkId == "data")
        {
            data = readWaveDataChunk(file.get());
        }
        else
        {
            // skip chunk
            uint32_t chunkSize;
            safeRead(&chunkSize, 4, 1, file.get());
            fseek(file.get(), chunkSize, SEEK_CUR);
        }
    }


    WaveFile waveFile;
    waveFile.header = header;
    waveFile.rawData = std::move(data);
    return waveFile;
}


WaveFileMono loadWaveFileToMono(const std::string& filename)
{
    auto waveFile = loadWaveFile(filename.c_str());
    std::vector<double> output;

    if (waveFile.header.audioFormat == WAVE_FORMAT_PCM)
    {
        switch(waveFile.header.bitsPerSample)
        {
            case 32:
            case 24:
            case 16:
            {
                std::vector<int32_t> samples;
                size_t bytesPerSample = waveFile.header.bitsPerSample / 8;
                samples.reserve(waveFile.rawData.size() / bytesPerSample);
                for (size_t i = 0; i < waveFile.rawData.size(); i += bytesPerSample)
                {
                    int32_t sample = 0;
                    int32_t shift = 4 - bytesPerSample;
                    for (size_t n = 0; n < bytesPerSample; ++n)
                    {
                        sample |= waveFile.rawData[i + n] << ( (n + shift) * 8);
                    }
                    samples.push_back(sample);
                }
                output = toDoubleVector(samples);
                break;
            }

            case 8:
            {
                // as 8bit samples are unsigned we cannot handle them in the general case above
                output = toDoubleVector(waveFile.rawData);
                break;
            }

            default:
            {   
                std::stringstream ss;
                ss << "Unsupported bits per sample: " << waveFile.header.bitsPerSample;
                throw std::runtime_error(ss.str());
            }
        }
    }
    else if (waveFile.header.audioFormat == WAVE_FORMAT_IEEE_FLOAT)
    {
        switch(waveFile.header.bitsPerSample)
        {
            case 32:
            case 64:
            {
                std::vector<double> samples;
                size_t bytesPerSample = waveFile.header.bitsPerSample / 8;
                samples.reserve(waveFile.rawData.size() / bytesPerSample);
                if (bytesPerSample == 4)
                {
                    for (size_t i = 0; i < waveFile.rawData.size(); i += bytesPerSample)
                    {
                        float sample;
                        memcpy(&sample, &waveFile.rawData[i], bytesPerSample);
                        samples.push_back(sample);
                    }
                }
                else if (bytesPerSample == 8)
                {
                    for (size_t i = 0; i < waveFile.rawData.size(); i += bytesPerSample)
                    {
                        double sample;
                        memcpy(&sample, &waveFile.rawData[i], bytesPerSample);
                        samples.push_back((float)sample);
                    }   
                }
                else
                {
                    throw std::runtime_error("Unsupported bits per sample float");
                }
                output = std::move(samples);
                break;
            }
            
            default:
            {
                std::stringstream ss;
                ss << "Unsupported bits per sample float: " << waveFile.header.bitsPerSample;
                throw std::runtime_error(ss.str());
            }
        }
    }
    else
    {
        std::stringstream ss;
        ss << "Unsupported audio format: " << waveFile.header.audioFormat;
        throw std::runtime_error(ss.str());
    }

    if (waveFile.header.numChannels > 1)
    {
        // merge float samples to mono
        for (size_t i = 0; i < output.size(); i += waveFile.header.numChannels)
        {
            float sample = 0;
            for (size_t j = 0; j < waveFile.header.numChannels; ++j)
            {
                sample += output[i + j];
            }
            sample /= waveFile.header.numChannels;
            output[i / waveFile.header.numChannels] = sample;
        }

        // remove unused samples
        output.resize(output.size() / waveFile.header.numChannels);
    }

    WaveFileMono waveFileMono;
    waveFileMono.sampleRate = waveFile.header.sampleRate;
    waveFileMono.data = std::move(output);
    return waveFileMono;
}

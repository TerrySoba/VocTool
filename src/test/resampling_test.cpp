#include <catch2/catch_test_macros.hpp>

#include "resampling.h"

#include "read_wave.h"

#include "test_helper.h"

#include <memory>

#include <iostream>
#include <fstream>
#include <sstream>

template <typename SampleType>
void dumpCsv(const std::vector<SampleType>& input, const std::string& filename)
{
    std::ofstream file(filename);
    for (size_t i = 0; i < input.size(); ++i)
    {
        file << (double) input[i] << "\n";
    }
}

template <typename SampleType>
void dumpRaw(const std::vector<SampleType>& input, const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    for (size_t i = 0; i < input.size(); ++i)
    {
        file.write((char*)&input[i], sizeof(SampleType));
    }
}

std::vector<uint8_t> readRaw(const std::string& filename)
{
    std::ifstream t(filename);
    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string str = buffer.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

TEST_CASE("Conversion Tests uint8_t")
{
    std::vector<uint8_t> input;
    for (uint16_t i = 0; i < 256; ++i)
    {
        input.push_back(i);
    }

    auto output = toDoubleVector(input);
    auto output2 = toUint8Vector(output);

    REQUIRE(output.size() == input.size());
    REQUIRE(output2.size() == input.size());
    REQUIRE(output2 == input);

}

TEST_CASE("Conversion Tests uint16_t")
{
    std::vector<int16_t> input;
    for (int32_t i = std::numeric_limits<int16_t>::min() + 1;
         i <= std::numeric_limits<int16_t>::max();
         ++i)
    {
        input.push_back(i);
    }

    auto output = toDoubleVector(input);
    std::vector<int16_t> output2 = toInt16Vector(output);

    REQUIRE(output.size() == input.size());
    REQUIRE(output2.size() == input.size());

    REQUIRE(input == output2);
}

TEST_CASE("Conversion Tests uint32_t")
{
    std::vector<int32_t> input;
    for (int32_t i = std::numeric_limits<int32_t>::min() + 1;
         i <= std::numeric_limits<int32_t>::min() + 100000;
         ++i)
    {
        input.push_back(i);
    }
    
    auto output = toDoubleVector(input);
    auto output2 = toInt32Vector(output);

    REQUIRE(output.size() == input.size());
    REQUIRE(output2.size() == input.size());

    REQUIRE(input == output2);
}



TEST_CASE("Normalize Test")
{
    std::vector<double> input = {1.1,2.2,3,4};

    normalizeSumToOne(input);

    // check that sum of elements is 1
    float sum = 0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        sum += input[i];
    }
    REQUIRE(sum == 1.0);
}

TEST_CASE("LoadWaveTest 16bit")
{
    auto waveFile = loadWaveFile(getTestDataDir() + "/16bit_mono_48000.wav");
    REQUIRE(waveFile.header.sampleRate == 48000);
    REQUIRE(waveFile.header.numChannels == 1);
    REQUIRE(waveFile.header.bitsPerSample == 16);
    REQUIRE(waveFile.header.bytesPerSample == 2);
    REQUIRE(waveFile.header.byteRate == 48000 * 2);
    REQUIRE(waveFile.header.audioFormat == WAVE_FORMAT_PCM);

    REQUIRE(
        readRaw(getTestDataDir() + "/16bit_mono_48000.raw") ==
        waveFile.rawData);
}

TEST_CASE("LoadWaveTest 24bit")
{
    auto waveFile = loadWaveFile(getTestDataDir() + "/24bit_mono_44100.wav");
    REQUIRE(waveFile.header.sampleRate == 44100);
    REQUIRE(waveFile.header.numChannels == 1);
    REQUIRE(waveFile.header.bitsPerSample == 24);
    REQUIRE(waveFile.header.bytesPerSample == 3);
    REQUIRE(waveFile.header.byteRate == 44100 * 3);
    REQUIRE(waveFile.header.audioFormat == WAVE_FORMAT_PCM);

    REQUIRE(
        readRaw(getTestDataDir() + "/24bit_mono_44100.raw") ==
        waveFile.rawData);
}

TEST_CASE("LoadWaveTest 32bit float")
{
    auto waveFile = loadWaveFile(getTestDataDir() + "/32bit_float_mono_48000.wav");
    REQUIRE(waveFile.header.sampleRate == 48000);
    REQUIRE(waveFile.header.numChannels == 1);
    REQUIRE(waveFile.header.bitsPerSample == 32);
    REQUIRE(waveFile.header.bytesPerSample == 4);
    REQUIRE(waveFile.header.byteRate == 48000 * 4);
    REQUIRE(waveFile.header.audioFormat == WAVE_FORMAT_IEEE_FLOAT);

    REQUIRE(
        readRaw(getTestDataDir() + "/32bit_float_mono_48000.raw") ==
        waveFile.rawData);
}

TEST_CASE("Resampling Test")
{
    std::vector<double> input(1000);
    // fill with defined frequency
    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] = (i&1) ? 1 : -1;
    }

    SECTION( "10Hz to 5Hz" )
    {
        std::vector<double> output = resample(input, 10, 5);
        REQUIRE(output.size() == 500);
        
    }

    SECTION( "100Hz to 33Hz" )
    {
        std::vector<double> output = resample(input, 100, 33);
        REQUIRE(output.size() == 330);
    }

    SECTION( "1000Hz to 200Hz" )
    {
        std::vector<double> output = resample(input, 1000, 200);
        REQUIRE(output.size() == 200);
    }
}




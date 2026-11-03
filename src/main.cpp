#include "file_tools.h"
#include "command_line_parser.h"
#include "voc_format.h"
#include "decode_creative_adpcm.h"
#include "encode_creative_adpcm.h"
#include "read_wave.h"
#include "resampling.h"
#include "detect_file_format.h"
#include "write_wave.h"
#include "compare_audio.h"

#include <iostream>
#include <map>

std::map <std::string, VocSampleFormat> compressionFormats =
{
    {"PCM", VOC_FORMAT_PCM_8BIT},
    {"ADPCM4", VOC_FORMAT_ADPCM_4BIT},
    {"ADPCM2", VOC_FORMAT_ADPCM_2BIT},
};

std::string vocSampleFormatToString(VocSampleFormat format)
{
    for (auto& elem : compressionFormats)
    {
        if (elem.second == format)
        {
            return elem.first;
        }
    }
    return "UNKNOWN";
}


void printEncodingDifference(std::vector<uint8_t>& raw, std::vector<uint8_t>& decoded, uint32_t sampleRate)
{
    auto difference = computeAudioDifference(raw, decoded, sampleRate);
    printf("  Average difference after encoding and decoding: %.2f\n", difference.averageDifference);
    printf("  Max difference after encoding and decoding: %.2f\n", difference.maxDifference);
}


int convertWaveToVoc(const clp::CommandLineParser& parser)
{
    VocSampleFormat format;
    try
    {
        format = compressionFormats.at(parser.getValue<std::string>("compression"));
    }
    catch (...)
    {
        printf("invalid compression format\n");
        return 1;
    }

    const NoiseShaping noiseShaping = noiseShapingFromString(parser.getValue<std::string>("shaping"));
    auto targetSampleRate = parser.getValueOptional<int32_t>("frequency");
    auto filename = parser.getValue<std::string>("input");
    auto waveFile = loadWaveFileToMono(filename.c_str());

    printf("Creating file %s, ", parser.getValue<std::string>("output").c_str());

    if (targetSampleRate.has_value() && *targetSampleRate != waveFile.sampleRate)
    {
        printf("resampling from %d Hz to %d Hz, ", waveFile.sampleRate, *targetSampleRate);
        // resample
        waveFile.data = resample(
            waveFile.data,
            waveFile.sampleRate,
            *targetSampleRate,
            resamplerQualityFromString(parser.getValue<std::string>("resampler")));
        waveFile.sampleRate = *targetSampleRate;
    }

    // if normalize is requested normalize data
    if (parser.hasValue("normalize"))
    {
        normalize(waveFile.data, parser.getValue<float>("normalize"));
    }

    auto raw = toUint8Vector(waveFile.data);

    std::vector<uint8_t> encodedSampleData;

    switch (format)
    {
    case VOC_FORMAT_ADPCM_4BIT:
    {
        printf("Output format: ADPCM 4-bit\n");
        encodedSampleData = createAdpcm4Bit(waveFile.data, noiseShaping);
        auto decodedSampleData = decodeAdpcm4(
            encodedSampleData.front(),
            std::span<uint8_t>(encodedSampleData).subspan(1));
        printEncodingDifference(raw, decodedSampleData, waveFile.sampleRate);
        break;
    }
    case VOC_FORMAT_ADPCM_2BIT:
    {
        printf("Output format: ADPCM 2-bit\n");
        encodedSampleData = createAdpcm2Bit(waveFile.data, noiseShaping);
        auto decodedSampleData = decodeAdpcm2(
            encodedSampleData.front(),
            std::vector<uint8_t>(encodedSampleData.begin() + 1, encodedSampleData.end()));
        printEncodingDifference(raw, decodedSampleData, waveFile.sampleRate);
        break;
    }
    case VOC_FORMAT_PCM_8BIT:
    {
        printf("Output format: PCM 8-bit\n");
        encodedSampleData = raw;
        break;
    }
    case VOC_FORMAT_ADPCM_3BIT:
    {
        printf("compression format not implemented\n");
        return 1;
    }
    }
    std::vector<uint8_t> vocData = createVocFile(waveFile.sampleRate, encodedSampleData, format);
    storeFile(parser.getValue<std::string>("output"), vocData);
    return 0;
}

int convertVocToWave(const clp::CommandLineParser& parser)
{
    auto inputFilename = parser.getValue<std::string>("input");
    auto outputFilename = parser.getValue<std::string>("output");

    auto vocData = readVocFile(inputFilename);

    std::cout << "Read VOC file " << inputFilename << std::endl;
    std::cout << "  Sample format: " << vocSampleFormatToString(vocData.sampleFormat) << " (" << vocData.sampleFormat << ")" << std::endl;
    std::cout << "  Sample size: " << vocData.sampleData.size() << std::endl;
    std::cout << "  Frequency: " << timeConstantToFrequency(vocData.timeConstant) << "Hz" << std::endl;
    std::cout << "  Time constant: " << (int)vocData.timeConstant << std::endl;
    std::cout << "  Major version: " << (int)vocData.majorVersion << std::endl;
    std::cout << "  Minor version: " << (int)vocData.minorVersion << std::endl;

    auto pcmVoc = decodeToPcm(vocData);

    std::cout << "Decoded VOC size " << pcmVoc.sampleData.size() << std::endl;

    write8bitMonoWaveFile(outputFilename, timeConstantToFrequency(vocData.timeConstant), pcmVoc.sampleData);
    return 0;
}


int main(int argc, char* argv[])
{
    try
    {
        clp::CommandLineParser parser(
            "Program to convert WAVE files into VOC files including optional ADPCM compression.\n"
            "Conversion from VOC to WAVE is also supported.\n"
            "File is converted to mono. If a frequency is give then the file is also resampled\n"
            "to the given frequency. Otherwise the sample frequency of the WAVE file is kept.\n\n"
            "Compression formats:\n"
            "  PCM    - unsigned integer 8-bit per sample\n"
            "  ADPCM4 - ADPCM 4-bit per sample\n"
            "  ADPCM2 - ADPCM 2-bit per sample\n");
        parser.addParameter("input", "i", "Name of the input file", clp::ParameterRequired::yes);
        parser.addParameter("output", "o", "Name of the output file", clp::ParameterRequired::yes);
        parser.addParameter("frequency", "f", "Frequency of output file in hertz", clp::ParameterRequired::no);
        parser.addParameter("compression", "c", "Compression to be used. Options: PCM, ADPCM4, ADPCM2", clp::ParameterRequired::no, "ADPCM4");
        parser.addParameter("normalize", "n", "Normalize audio to given fraction, e.g. 0.9", clp::ParameterRequired::no);
        parser.addParameter("shaping", "s", "Noise shaping for ADPCM (as in ADPCM-XQ). Options: off, dynamic (adapts to the audio), static (high-pass, same as 1.0) or a fixed weight w with -1 < w <= 1. Positive weights move noise to high frequencies, negative to low frequencies.", clp::ParameterRequired::no, "off");
        parser.addParameter("resampler", "r", "Resampler quality (libsamplerate). Options: best, medium, fast, linear", clp::ParameterRequired::no, "best");
        parser.parse(argc, argv);

        auto detectedFormat = detectFileFormat(parser.getValue<std::string>("input"));

        if (detectedFormat == FileFormat::WAV)
        {
            return convertWaveToVoc(parser);
        }
        else if (detectedFormat == FileFormat::VOC)
        {
            return convertVocToWave(parser);
        }
        else
        {
            printf("Input file is not a WAVE or VOC file\n");
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        printf("Error: %s\n", e.what());
        return 1;
    }
}

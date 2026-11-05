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
#include "psychoacoustic.h"
#include "shaping_search.h"

#include <iostream>
#include <map>

std::map <std::string, VocSampleFormat> compressionFormats =
{
    {"PCM", VOC_FORMAT_PCM_8BIT},
    {"ADPCM4", VOC_FORMAT_ADPCM_4BIT},
    {"ADPCM3", VOC_FORMAT_ADPCM_3BIT},
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


struct SampleEncoding
{
    const char* description;
    AdpcmEncodeFunction encode;
    AdpcmDecodeFunction decode;
};

SampleEncoding sampleEncoding(VocSampleFormat format)
{
    auto withoutReference = [](const std::vector<uint8_t>& encoded) {
        return std::vector<uint8_t>(encoded.begin() + 1, encoded.end());
    };

    switch (format)
    {
    case VOC_FORMAT_PCM_8BIT:
        return {"PCM 8-bit", createPcm8Bit, [](const std::vector<uint8_t>& encoded) { return encoded; }};
    case VOC_FORMAT_ADPCM_4BIT:
        return {"ADPCM 4-bit", createAdpcm4Bit, [=](const std::vector<uint8_t>& encoded) {
            auto data = withoutReference(encoded);
            return decodeAdpcm4(encoded.front(), std::span<uint8_t>(data));
        }};
    case VOC_FORMAT_ADPCM_3BIT:
        return {"ADPCM 2.6-bit", createAdpcm3Bit, [=](const std::vector<uint8_t>& encoded) {
            return decodeAdpcm3(encoded.front(), withoutReference(encoded));
        }};
    case VOC_FORMAT_ADPCM_2BIT:
        return {"ADPCM 2-bit", createAdpcm2Bit, [=](const std::vector<uint8_t>& encoded) {
            return decodeAdpcm2(encoded.front(), withoutReference(encoded));
        }};
    }
    throw std::logic_error("Unknown sample format");
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

    const std::string shapingText = parser.getValue<std::string>("shaping");
    const bool automaticShaping = (shapingText == "auto");
    const NoiseShaping noiseShaping = automaticShaping ? NoiseShaping{} : noiseShapingFromString(shapingText);
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

    const SampleEncoding encoding = sampleEncoding(format);
    printf("Output format: %s\n", encoding.description);

    if (automaticShaping)
    {
        printf("  Trying noise shaping settings (perceptual noise, lower is better; low-frequency drift in levels):\n");
        auto candidates = evaluateShapings(
            waveFile.data, waveFile.sampleRate, defaultShapingCandidates(), encoding.encode, encoding.decode);
        const size_t best = bestShapingCandidate(candidates);
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            printf("    %-8s %7.2f dB  drift %5.1f%s\n",
                toString(candidates[i].shaping).c_str(),
                candidates[i].perceptualNoise,
                candidates[i].drift,
                (i == best) ? "  <- selected" : (candidates[i].drift > kMaxLowFrequencyDrift) ? "  (drifts too much)" : "");
        }
        encodedSampleData = std::move(candidates[best].encoded);
    }
    else
    {
        encodedSampleData = encoding.encode(waveFile.data, noiseShaping);
    }

    auto decodedSampleData = encoding.decode(encodedSampleData);
    printEncodingDifference(raw, decodedSampleData, waveFile.sampleRate);
    const PerceptualModel model(waveFile.data, waveFile.sampleRate);
    printf("  Perceptual noise (noise-to-mask ratio): %.2f dB\n", model.noiseToMaskRatio(decodedSampleData));
    printf("  Low-frequency drift: %.1f levels\n", model.lowFrequencyDrift(decodedSampleData));

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
            "  ADPCM3 - ADPCM 2.6-bit per sample (3 samples per byte)\n"
            "  ADPCM2 - ADPCM 2-bit per sample\n");
        parser.addParameter("input", "i", "Name of the input file", clp::ParameterRequired::yes);
        parser.addParameter("output", "o", "Name of the output file", clp::ParameterRequired::yes);
        parser.addParameter("frequency", "f", "Frequency of output file in hertz", clp::ParameterRequired::no);
        parser.addParameter("compression", "c", "Compression to be used. Options: PCM, ADPCM4, ADPCM3, ADPCM2", clp::ParameterRequired::no, "ADPCM4");
        parser.addParameter("normalize", "n", "Normalize audio to given fraction, e.g. 0.9", clp::ParameterRequired::no);
        parser.addParameter("shaping", "s", "Noise shaping for PCM and ADPCM (as in ADPCM-XQ). Options: off, auto (tries several settings and keeps the best according to a psychoacoustic model), dynamic (adapts to the audio), static (high-pass, same as 1.0) or a fixed weight w with -1 < w <= 1. Positive weights move noise to high frequencies, negative to low frequencies.", clp::ParameterRequired::no, "off");
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

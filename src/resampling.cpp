#include "resampling.h"

#include <stdint.h>
#include <limits>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <cmath>

#include <samplerate.h>

void normalizeSumToOne(std::vector<double>& input)
{
    double sum = 0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        sum += input[i];
    }

    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] /= sum;
    }
}

void normalize(std::vector<double>& input, double fraction)
{
    double max = 0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (std::abs(input[i]) > max)
        {
            max = std::abs(input[i]);
        }
    }

    max /= fraction;

    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] /= max;
    }
}

std::vector<double> toDoubleVector(const std::vector<int32_t>& input)
{
    std::vector<double> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(std::clamp(input[i] / (double)std::numeric_limits<int32_t>::max(), -1.0, 1.0));
    }
    return output;
}

std::vector<double> toDoubleVector(const std::vector<int16_t>& input)
{
    std::vector<double> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(std::clamp(input[i] / (double)std::numeric_limits<int16_t>::max(), -1.0, 1.0));
    }
    return output;
}

std::vector<double> toDoubleVector(const std::vector<uint8_t>& input)
{
    std::vector<double> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(std::clamp((input[i] - 128.0) / 128.0, -1.0, 1.0));
    }
    return output;
}

std::vector<uint8_t> toUint8Vector(const std::vector<double>& input)
{
    std::vector<uint8_t> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(static_cast<uint8_t>(std::round(std::clamp(input[i] * 128.0 + 128.0, 0.0, 255.0))));
    }
    return output;
}

std::vector<int16_t> toInt16Vector(const std::vector<double>& input)
{
    std::vector<int16_t> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(static_cast<int16_t>(std::round(
            std::clamp(
                input[i] * std::numeric_limits<int16_t>::max(),
                (double)std::numeric_limits<int16_t>::min(),
                (double)std::numeric_limits<int16_t>::max()))));
    }
    return output;
}

std::vector<int32_t> toInt32Vector(const std::vector<double>& input)
{
    std::vector<int32_t> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(static_cast<int32_t>(std::round(
            std::clamp(
                input[i] * std::numeric_limits<int32_t>::max(),
                (double)std::numeric_limits<int32_t>::min(),
                (double)std::numeric_limits<int32_t>::max()))));
    }
    return output;
}


int toLibsamplerateConverter(ResamplerQuality quality)
{
    switch (quality)
    {
        case ResamplerQuality::best:   return SRC_SINC_BEST_QUALITY;
        case ResamplerQuality::medium: return SRC_SINC_MEDIUM_QUALITY;
        case ResamplerQuality::fast:   return SRC_SINC_FASTEST;
        case ResamplerQuality::linear: return SRC_LINEAR;
    }
    throw std::runtime_error("Invalid resampler quality");
}

ResamplerQuality resamplerQualityFromString(const std::string& name)
{
    if (name == "best")   return ResamplerQuality::best;
    if (name == "medium") return ResamplerQuality::medium;
    if (name == "fast")   return ResamplerQuality::fast;
    if (name == "linear") return ResamplerQuality::linear;
    throw std::runtime_error("Invalid resampler quality: " + name);
}

std::vector<double> resample(
    const std::vector<double>& inputData,
    uint32_t inputSampleRate,
    uint32_t outputSampleRate,
    ResamplerQuality quality)
{
    if (inputSampleRate == 0 || outputSampleRate == 0)
    {
        throw std::runtime_error("Sample rate must not be zero");
    }
    if (inputData.empty())
    {
        return {};
    }

    const double ratio = (double)outputSampleRate / (double)inputSampleRate;
    if (!src_is_valid_ratio(ratio))
    {
        throw std::runtime_error("Resampling ratio is not supported by libsamplerate (must be between 1/256 and 256)");
    }

    std::vector<float> input(inputData.begin(), inputData.end());
    const uint64_t outputSize = (uint64_t)inputData.size() * (uint64_t)outputSampleRate / (uint64_t)inputSampleRate;
    // libsamplerate may produce a few frames more than the nominal size, so leave some headroom
    std::vector<float> output(outputSize + 16);

    SRC_DATA srcData{};
    srcData.data_in = input.data();
    srcData.input_frames = (long)input.size();
    srcData.data_out = output.data();
    srcData.output_frames = (long)output.size();
    srcData.src_ratio = ratio;
    srcData.end_of_input = 1;

    int error = src_simple(&srcData, toLibsamplerateConverter(quality), 1);
    if (error != 0)
    {
        throw std::runtime_error(std::string("libsamplerate error: ") + src_strerror(error));
    }

    output.resize(std::min<uint64_t>((uint64_t)srcData.output_frames_gen, outputSize));
    return std::vector<double>(output.begin(), output.end());
}

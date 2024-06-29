#include "resampling.h"

#include <stdint.h>
#include <limits>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <cmath>

std::vector<double> blackmanWindow(size_t length)
{
    std::vector<double> output;
    output.reserve(length);
    for (size_t i = 0; i < length; ++i)
    {
        output.push_back(0.42 - 0.5 * cos(2.0 * M_PI * i / (length - 1)) + 0.08 * cos(4.0 * M_PI * i / (length - 1)));
    }
    return output;
}

std::vector<double> sinc(double sampleRate, double cutoffFrequency, size_t length)
{
    std::vector<double> output;
    output.reserve(length);
    for (size_t i = 0; i < length; ++i)
    {
        double x = (double)i - (double)length / 2.0;
        if (x == 0)
        {
            output.push_back(2.0 * M_PI * cutoffFrequency / sampleRate);
        }
        else
        {
            output.push_back(sin(2.0 * M_PI * cutoffFrequency * x / sampleRate) / x);
        }
    }
    return output;
}

std::vector<double> multiplyVectors(const std::vector<double>& a, const std::vector<double>& b)
{
    std::vector<double> output;
    output.reserve(a.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        output.push_back(a[i] * b[i]);
    }
    return output;
}

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

/**
 * @brief Creates a lowpass filter with the given parameters.
 * 
 * @param sampleRate The sample rate of the input signal in Hz.
 * @param cutoffFrequency The cutoff frequency of the filter in Hz.
 * @param transitionBandwidth The transition bandwidth of the filter in Hz.
 * 
 * The created filter needs to be applied to the signal to be filtered using convolution.
 */
std::vector<double> createLowpassFilter(double sampleRate, double cutoffFrequency, double transitionBandwidth)
{
    // calculate filter length
    size_t length = 4 * sampleRate / transitionBandwidth;
    if (length % 2 == 0) ++length;  // ensure length is odd

    // generate sinc
    std::vector<double> mySinc = sinc(sampleRate, cutoffFrequency, length);

    // multiply with window
    std::vector<double> window = blackmanWindow(length);
    std::vector<double> sincWindowed = multiplyVectors(mySinc, window);

    normalizeSumToOne(sincWindowed);
    return sincWindowed;
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
        output.push_back(std::round(std::clamp(input[i] * 128.0 + 128.0, 0.0, 255.0)));
    }
    return output;
}

std::vector<int16_t> toInt16Vector(const std::vector<double>& input)
{
    std::vector<int16_t> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(std::round(
            std::clamp(
                input[i] * std::numeric_limits<int16_t>::max(),
                (double)std::numeric_limits<int16_t>::min(),
                (double)std::numeric_limits<int16_t>::max())));
    }
    return output;
}

std::vector<int32_t> toInt32Vector(const std::vector<double>& input)
{
    std::vector<int32_t> output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        output.push_back(std::round(
            std::clamp(
                input[i] * std::numeric_limits<int32_t>::max(),
                (double)std::numeric_limits<int32_t>::min(),
                (double)std::numeric_limits<int32_t>::max())));
    }
    return output;
}


std::vector<double> convolution(const std::vector<double>& input, const std::vector<double>& kernel)
{
    if (kernel.size() > input.size())
    {
        throw std::runtime_error("Kernel size must be smaller than input size!");
    }

    if (kernel.size() % 2 == 0)
    {
        throw std::runtime_error("Kernel size must be odd!");
    }

    std::vector<double> output;
    output.reserve(input.size() + kernel.size() - 1);
    for (size_t i = 0; i < input.size() + kernel.size() - 1; ++i)
    {
        double sample = 0;
        for (size_t j = 0; j < kernel.size(); ++j)
        {
            if (i >= j && i - j < input.size())
            {
                sample += input[i - j] * kernel[j];
            }
        }
        output.push_back(sample);
    }

    // trim size to input size by removing samples from the beginning and end
    size_t trimSize = kernel.size() / 2;
    output.erase(output.begin(), output.begin() + trimSize);
    output.erase(output.end() - trimSize, output.end());
    return output;
}

std::vector<double> lowPassFilter(const std::vector<double>& input, double sampleRate, double cutoffFrequency, double transitionBandwidth)
{
    auto filter = createLowpassFilter(sampleRate, cutoffFrequency, transitionBandwidth);
    return convolution(input, filter);
}


std::vector<double> resample(
    const std::vector<double>& inputData,
    uint32_t inputSampleRate,
    uint32_t outputSampleRate,
    std::optional<double> cutoffFrequency,
    std::optional<double> transitionBandwidth)
{
    // first lowpass filter signal to prevent aliasing
    auto input = lowPassFilter(
        inputData,
        inputSampleRate,
        cutoffFrequency.value_or(outputSampleRate / 2.0),
        transitionBandwidth.value_or(outputSampleRate / 10.0));
    uint64_t outputSize = (uint64_t)input.size() * (uint64_t)outputSampleRate / (uint64_t)inputSampleRate;
    // std::cout << "outputSize = " << outputSize << "\n";
    std::vector<double> output(outputSize);
    // output.reserve(outputSize);
    for (size_t i = 0; i < outputSize; ++i)
    {
        double inputIndex = (double)i * (double)inputSampleRate / (double)outputSampleRate;
        
        // do linear interpolation
        size_t inputIndexFloor = (size_t)inputIndex;
        size_t inputIndexCeil = inputIndexFloor + 1;
        double inputIndexFraction = inputIndex - inputIndexFloor;
        if (inputIndexCeil >= input.size())
        {
            inputIndexCeil = input.size() - 1;
        }
        double sample = ((1.0 - inputIndexFraction) * input[inputIndexFloor] + inputIndexFraction * input[inputIndexCeil]);

        output[i] = sample;
    }
    return output;
}

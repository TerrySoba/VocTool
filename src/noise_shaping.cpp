/*
 * The dynamic noise shaping in this file is ported from adpcm-dns.c of ADPCM-XQ:
 *
 *                     Copyright (c) David Bryant
 *                        All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Conifer Software nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noise_shaping.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr int kFilterLength = 15;
constexpr int kWindowLength = 101;

// Lower limit for dynamic shaping weights, as used by ADPCM-XQ (-1024 would mean infinite DC boost).
constexpr int16_t kDynamicMinimumWeight = -512;

/**
 * Applies a box filter of the given half width and returns the averaged magnitude (RMS) at each point.
 * At the ends only the visible samples are used.
 */
void windowAverage(std::vector<float>& samples, int halfWidth)
{
    const int count = static_cast<int>(samples.size());
    std::vector<float> output(count);
    double sum = 0.0;
    int m = 0, n = 0;

    for (int i = 0; i < count; ++i)
    {
        const int k = std::min(i + halfWidth + 1, count);
        const int j = std::max(i - halfWidth, 0);

        while (m < j)
        {
            if ((sum -= samples[m] * samples[m]) < 0.0) sum = 0.0;
            m++;
        }

        while (n < k)
        {
            sum += samples[n] * samples[n];
            n++;
        }

        output[i] = static_cast<float>(std::sqrt(sum / (n - m)));
    }

    samples = std::move(output);
}

/**
 * Generates one shaping value per sample by filtering the audio at fs/6 and comparing the averaged
 * levels above and below that frequency. Output values are in the range minValue..1024, where 1024
 * means first-order high-frequency shaping and negative values low-frequency shaping.
 * Port of generate_dns_values() from ADPCM-XQ (mono only, starting value 0).
 */
std::vector<int16_t> generateDynamicShapingValues(const std::vector<int32_t>& samples, int16_t minValue)
{
    const float dBOffset = 7.3f, dBScaler = 64.0f;
    const int sampleCount = static_cast<int>(samples.size());
    const int filteredCount = sampleCount - kFilterLength + 1;
    const int16_t lastValue = 0;

    std::vector<int16_t> values(sampleCount, 0);

    if (filteredCount <= 1)
    {
        return values;
    }

    std::vector<float> lowFreq(filteredCount), highFreq(filteredCount);

    // 15-tap Hann windowed sinc lowpass at exactly fs/6. It is linear phase with an odd number of
    // taps, so the high band is the input minus the low band.
    for (int i = 0; i < filteredCount; ++i)
    {
        const int32_t* s = &samples[i];
        const float filterSum = static_cast<float>(
            (static_cast<double>(s[0]) + s[14]) *  0.00150031 +
            (static_cast<double>(s[2]) + s[12]) * -0.01703392 +
            (static_cast<double>(s[3]) + s[11]) * -0.03449186 +
            (static_cast<double>(s[5]) + s[ 9]) *  0.11776258 +
            (static_cast<double>(s[6]) + s[ 8]) *  0.26543272 +
             static_cast<double>(s[7])          *  0.33366033);

        highFreq[i] = s[kFilterLength >> 1] - filterSum;
        lowFreq[i] = filterSum;
    }

    // First-order difference on the low band, frequencies far below fs/6 matter less.
    for (int i = filteredCount - 1; i > 0; --i)
    {
        lowFreq[i] -= lowFreq[i - 1];
    }
    lowFreq[0] = lowFreq[1];

    windowAverage(lowFreq, kWindowLength >> 1);
    windowAverage(highFreq, kWindowLength >> 1);

    const float maxDB = 1024 / dBScaler - dBOffset;
    const float minDB = minValue / dBScaler - dBOffset;
    const float maxRatio = std::pow(10.0f, maxDB / 20.0f);
    const float minRatio = std::pow(10.0f, minDB / 20.0f);

    for (int i = 0; i < filteredCount; ++i)
    {
        if (highFreq[i] > 1.0f && lowFreq[i] > 1.0f)
        {
            const float ratio = highFreq[i] / lowFreq[i];
            int shapingValue;

            if (ratio >= maxRatio)
                shapingValue = 1024;
            else if (ratio <= minRatio)
                shapingValue = minValue;
            else
                shapingValue = static_cast<int>(std::floor((std::log10(ratio) * 20.0f + dBOffset) * dBScaler + 0.5f));

            values[i + (kFilterLength >> 1)] = static_cast<int16_t>(shapingValue);
        }
    }

    // interpolate the first values from lastValue to the first calculated value
    for (int i = 0; i < kFilterLength >> 1; ++i)
    {
        values[i] = static_cast<int16_t>(
            (static_cast<int32_t>(values[kFilterLength >> 1]) * (i + 1) +
             static_cast<int32_t>(lastValue) * ((kFilterLength >> 1) - i) +
             (kFilterLength >> 2)) / ((kFilterLength >> 1) + 1));
    }

    // repeat the last calculated value at the end
    for (int i = filteredCount + (kFilterLength >> 1); i < sampleCount; ++i)
    {
        values[i] = values[(kFilterLength >> 1) + filteredCount - 1];
    }

    return values;
}

} // namespace


NoiseShaping noiseShapingFromString(const std::string& text)
{
    if (text == "off") return {NoiseShapingMode::off};
    if (text == "dynamic") return {NoiseShapingMode::dynamic};
    if (text == "static") return {NoiseShapingMode::fixed, 1.0};

    double weight;
    size_t parsed = 0;
    try
    {
        weight = std::stod(text, &parsed);
    }
    catch (...)
    {
        parsed = 0;
    }

    if (parsed == 0 || parsed != text.size())
    {
        throw std::runtime_error("Invalid noise shaping: " + text + " (use off, dynamic, static or a weight -1 < w <= 1)");
    }
    if (!(weight > -1.0 && weight <= 1.0))
    {
        throw std::runtime_error("Noise shaping weight must be in range -1 < w <= 1");
    }
    if (weight == 0.0)
    {
        return {NoiseShapingMode::off};
    }
    return {NoiseShapingMode::fixed, weight};
}


std::vector<int16_t> computeShapingWeights(const std::vector<int32_t>& samples, const NoiseShaping& shaping)
{
    switch (shaping.mode)
    {
    case NoiseShapingMode::off:
        return {};
    case NoiseShapingMode::fixed:
    {
        const int weight = std::clamp(static_cast<int>(std::floor(shaping.weight * 1024.0 + 0.5)), -1023, 1024);
        return std::vector<int16_t>(samples.size(), static_cast<int16_t>(weight));
    }
    case NoiseShapingMode::dynamic:
        return generateDynamicShapingValues(samples, kDynamicMinimumWeight);
    }
    return {};
}

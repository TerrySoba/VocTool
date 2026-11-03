#ifndef RESAMPLING_H
#define RESAMPLING_H

#include <cstdint>
#include <vector>
#include <string>

enum class ResamplerQuality
{
    best,    // libsamplerate SRC_SINC_BEST_QUALITY
    medium,  // libsamplerate SRC_SINC_MEDIUM_QUALITY
    fast,    // libsamplerate SRC_SINC_FASTEST
    linear   // libsamplerate SRC_LINEAR
};

ResamplerQuality resamplerQualityFromString(const std::string& name);

/**
 * @brief Resamples mono audio data using libsamplerate.
 *
 * Anti-aliasing filtering is handled by libsamplerate's band-limited sinc converters.
 */
std::vector<double> resample(
    const std::vector<double>& inputData,
    uint32_t inputSampleRate,
    uint32_t outputSampleRate,
    ResamplerQuality quality = ResamplerQuality::best);

std::vector<double> toDoubleVector(const std::vector<int32_t>& input);
std::vector<double> toDoubleVector(const std::vector<int16_t>& input);
std::vector<double> toDoubleVector(const std::vector<uint8_t>& input);
std::vector<uint8_t> toUint8Vector(const std::vector<double>& input);
std::vector<int16_t> toInt16Vector(const std::vector<double>& input);
std::vector<int32_t> toInt32Vector(const std::vector<double>& input);

void normalize(std::vector<double>& input, double fraction);
void normalizeSumToOne(std::vector<double>& input);


#endif

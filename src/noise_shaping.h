#ifndef NOISE_SHAPING_H
#define NOISE_SHAPING_H

#include <cstdint>
#include <string>
#include <vector>

/*
 * Noise shaping as implemented in ADPCM-XQ by David Bryant (https://github.com/dbry/adpcm-xq).
 *
 * The encoder feeds back the previous quantization error into the next target sample, which moves
 * the spectrum of the quantization noise up or down in frequency:
 *   - fixed:   a constant shaping weight w with -1 < w <= 1. Positive values move the noise to high
 *              frequencies (w = 1 is first-order high-pass shaping), negative values to low frequencies.
 *   - dynamic: the weight is chosen per sample depending on the spectrum of the audio, so that the
 *              noise is more likely to be masked by the signal.
 * Noise shaping increases the total noise power, but can make the noise less audible.
 */
enum class NoiseShapingMode
{
    off,
    fixed,
    dynamic
};

struct NoiseShaping
{
    NoiseShapingMode mode = NoiseShapingMode::off;
    double weight = 1.0;  // only used for NoiseShapingMode::fixed
};

/**
 * Parses "off", "dynamic", "static" (fixed weight 1.0) or a fixed weight w with -1 < w <= 1.
 * A weight of 0 disables noise shaping.
 */
NoiseShaping noiseShapingFromString(const std::string& text);

/**
 * Returns one shaping weight per sample, in units of 1/1024 (1024 = weight 1.0).
 * Returns an empty vector if noise shaping is off.
 *
 * @param samples Signed audio samples in 16-bit scale.
 */
std::vector<int16_t> computeShapingWeights(const std::vector<int32_t>& samples, const NoiseShaping& shaping);

#endif

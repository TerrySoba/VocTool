#ifndef SHAPING_SEARCH_H
#define SHAPING_SEARCH_H

#include "noise_shaping.h"

#include <cstdint>
#include <functional>
#include <vector>

/*
 * Automatic selection of the noise shaping: the audio is encoded with several noise shaping settings,
 * every result is decoded and rated with the psychoacoustic model (see psychoacoustic.h), and the
 * encoding with the least audible noise is used.
 */

using AdpcmEncodeFunction = std::function<std::vector<uint8_t>(const std::vector<double>& samples, const NoiseShaping& shaping)>;
using AdpcmDecodeFunction = std::function<std::vector<uint8_t>(const std::vector<uint8_t>& encoded)>;

struct ShapingCandidate
{
    NoiseShaping shaping;
    std::vector<uint8_t> encoded;
    double perceptualNoise;  // total noise-to-mask ratio in dB, lower is better
};

/** The settings tried by "auto": off, dynamic and fixed weights from low-pass to high-pass. */
std::vector<NoiseShaping> defaultShapingCandidates();

/**
 * Encodes samples with every candidate shaping (in parallel) and rates the decoded results.
 * Returns the results in the order of candidates.
 *
 * @param samples    Samples in range -1.0 .. 1.0.
 * @param sampleRate Sample rate in Hz, needed by the psychoacoustic model.
 */
std::vector<ShapingCandidate> evaluateShapings(
    const std::vector<double>& samples,
    uint32_t sampleRate,
    const std::vector<NoiseShaping>& candidates,
    const AdpcmEncodeFunction& encode,
    const AdpcmDecodeFunction& decode);

/** Returns the index of the candidate with the lowest perceptual noise (the first one on ties). */
size_t bestShapingCandidate(const std::vector<ShapingCandidate>& candidates);

#endif

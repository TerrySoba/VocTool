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
 *
 * Encodings whose output drifts away from the original at very low frequencies by more than
 * kMaxLowFrequencyDrift are not used (unless all of them do). The psychoacoustic model does not
 * hear such a drift, but it costs headroom, which makes the output clip, and can leave an offset
 * at the end. Strong low-frequency shaping can cause it.
 */

/** Largest accepted low-frequency drift in 8-bit levels, see PerceptualModel::lowFrequencyDrift(). */
constexpr double kMaxLowFrequencyDrift = 4.0;

using AdpcmEncodeFunction = std::function<std::vector<uint8_t>(const std::vector<double>& samples, const NoiseShaping& shaping)>;
using AdpcmDecodeFunction = std::function<std::vector<uint8_t>(const std::vector<uint8_t>& encoded)>;

struct ShapingCandidate
{
    NoiseShaping shaping;
    std::vector<uint8_t> encoded;
    double perceptualNoise;  // total noise-to-mask ratio in dB, lower is better
    double drift;            // low-frequency drift in 8-bit levels, see PerceptualModel::lowFrequencyDrift()
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

/**
 * Returns the index of the candidate with the lowest perceptual noise (the first one on ties) among
 * the candidates with a drift of at most kMaxLowFrequencyDrift. If all of them drift more, the one
 * with the least drift is returned.
 */
size_t bestShapingCandidate(const std::vector<ShapingCandidate>& candidates);

#endif

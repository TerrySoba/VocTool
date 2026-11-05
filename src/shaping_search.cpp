#include "shaping_search.h"
#include "psychoacoustic.h"

#include <future>
#include <stdexcept>

std::vector<NoiseShaping> defaultShapingCandidates()
{
    std::vector<NoiseShaping> candidates = {{NoiseShapingMode::off}, {NoiseShapingMode::dynamic}};
    for (double weight : {-0.99, -0.9, -0.75, -0.5, -0.25, 0.25, 0.5, 0.75, 1.0})
    {
        candidates.push_back({NoiseShapingMode::fixed, weight});
    }
    return candidates;
}

std::vector<ShapingCandidate> evaluateShapings(
    const std::vector<double>& samples,
    uint32_t sampleRate,
    const std::vector<NoiseShaping>& candidates,
    const AdpcmEncodeFunction& encode,
    const AdpcmDecodeFunction& decode)
{
    const PerceptualModel model(samples, sampleRate);

    // Every candidate is an independent encoding, so they run in parallel.
    std::vector<std::future<ShapingCandidate>> futures;
    futures.reserve(candidates.size());
    for (const NoiseShaping& shaping : candidates)
    {
        futures.push_back(std::async(std::launch::async, [&, shaping]() {
            ShapingCandidate candidate{shaping, encode(samples, shaping), 0.0, 0.0};
            const std::vector<uint8_t> decoded = decode(candidate.encoded);
            candidate.perceptualNoise = model.noiseToMaskRatio(decoded);
            candidate.drift = model.lowFrequencyDrift(decoded);
            return candidate;
        }));
    }

    std::vector<ShapingCandidate> results;
    results.reserve(candidates.size());
    for (auto& future : futures)
    {
        results.push_back(future.get());
    }
    return results;
}

size_t bestShapingCandidate(const std::vector<ShapingCandidate>& candidates)
{
    if (candidates.empty())
    {
        throw std::logic_error("No noise shaping candidates");
    }

    auto acceptable = [](const ShapingCandidate& candidate) { return candidate.drift <= kMaxLowFrequencyDrift; };

    size_t best = 0;
    for (size_t i = 1; i < candidates.size(); ++i)
    {
        const ShapingCandidate& candidate = candidates[i];
        const ShapingCandidate& current = candidates[best];
        const bool better = (acceptable(candidate) != acceptable(current))
            ? acceptable(candidate)
            : acceptable(candidate) ? (candidate.perceptualNoise < current.perceptualNoise) : (candidate.drift < current.drift);
        if (better)
        {
            best = i;
        }
    }
    return best;
}

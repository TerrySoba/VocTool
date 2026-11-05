#include "catch_importer.h"

#include "psychoacoustic.h"
#include "shaping_search.h"
#include "encode_creative_adpcm.h"
#include "decode_creative_adpcm.h"
#include "read_wave.h"
#include "resampling.h"
#include "test_helper.h"

#include <cmath>
#include <random>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const uint32_t kSampleRate = 11025;

// A reference signal that is exactly representable in 8 bit, so the noise is exactly what is added.
struct TestSignal
{
    std::vector<double> reference;
    std::vector<int> values;  // -128 .. 127
};

TestSignal tone(double frequency, double amplitude, size_t length)
{
    TestSignal signal;
    for (size_t i = 0; i < length; ++i)
    {
        const int value = static_cast<int>(std::lround(amplitude * 128.0 * std::sin(2 * kPi * frequency * i / kSampleRate)));
        signal.values.push_back(value);
        signal.reference.push_back(value / 128.0);
    }
    return signal;
}

std::vector<uint8_t> withNoise(const TestSignal& signal, const std::vector<int>& noise)
{
    std::vector<uint8_t> decoded;
    for (size_t i = 0; i < signal.values.size(); ++i)
    {
        decoded.push_back(static_cast<uint8_t>(std::clamp(signal.values[i] + noise[i] + 128, 0, 255)));
    }
    return decoded;
}

std::vector<int> sineNoise(double frequency, double amplitude, size_t length)
{
    std::vector<int> noise;
    for (size_t i = 0; i < length; ++i)
    {
        noise.push_back(static_cast<int>(std::lround(amplitude * std::sin(2 * kPi * frequency * i / kSampleRate + 1.0))));
    }
    return noise;
}

} // namespace

TEST_CASE("Perceptual noise of identical signal is minimal")
{
    auto signal = tone(1000, 0.5, 4000);
    auto decoded = withNoise(signal, std::vector<int>(4000, 0));
    REQUIRE(perceptualNoise(signal.reference, decoded, kSampleRate) == kMinimumPerceptualNoiseDb);
}

TEST_CASE("Perceptual noise grows with noise energy")
{
    auto signal = tone(500, 0.4, 4000);
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> distribution(-1, 1);
    std::vector<int> noise(4000), louderNoise(4000);
    for (size_t i = 0; i < noise.size(); ++i)
    {
        noise[i] = distribution(gen);
        louderNoise[i] = 4 * noise[i];
    }

    const PerceptualModel model(signal.reference, kSampleRate);
    const double quiet = model.noiseToMaskRatio(withNoise(signal, noise));
    const double loud = model.noiseToMaskRatio(withNoise(signal, louderNoise));

    // the masking threshold only depends on the reference, so 16 times the energy is exactly 12.04 dB
    REQUIRE(std::abs(loud - quiet - 10.0 * std::log10(16.0)) < 1e-6);
}

TEST_CASE("Noise close to a loud tone is masked")
{
    auto signal = tone(1000, 0.6, 6000);
    const PerceptualModel model(signal.reference, kSampleRate);

    const double nearTone = model.noiseToMaskRatio(withNoise(signal, sineNoise(1100, 3, 6000)));
    const double farFromTone = model.noiseToMaskRatio(withNoise(signal, sineNoise(3500, 3, 6000)));
    const double belowTone = model.noiseToMaskRatio(withNoise(signal, sineNoise(500, 3, 6000)));

    REQUIRE(nearTone + 10.0 < farFromTone);
    // masking spreads more to higher than to lower frequencies
    REQUIRE(nearTone < belowTone);
}

TEST_CASE("Perceptual model handles short and empty input")
{
    REQUIRE(perceptualNoise({}, {}, kSampleRate) == kMinimumPerceptualNoiseDb);

    auto signal = tone(1000, 0.5, 10);
    REQUIRE(std::isfinite(perceptualNoise(signal.reference, withNoise(signal, std::vector<int>(10, 2)), kSampleRate)));

    REQUIRE_THROWS(perceptualNoise(signal.reference, {128, 128}, kSampleRate));
}

TEST_CASE("Automatic noise shaping selects the least audible candidate")
{
    std::vector<double> samples(4000);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = 0.5 * std::sin(2 * kPi * 2500 * i / kSampleRate) + 0.1 * std::sin(2 * kPi * 300 * i / kSampleRate);
    }

    auto decode = [](const std::vector<uint8_t>& encoded) {
        return decodeAdpcm2(encoded.front(), std::vector<uint8_t>(encoded.begin() + 1, encoded.end()));
    };

    const auto shapings = defaultShapingCandidates();
    const auto candidates = evaluateShapings(samples, kSampleRate, shapings, createAdpcm2Bit, decode);
    REQUIRE(candidates.size() == shapings.size());

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        // results are in the order of the candidates and match a direct encoding
        REQUIRE(candidates[i].shaping.mode == shapings[i].mode);
        REQUIRE(candidates[i].shaping.weight == shapings[i].weight);
        REQUIRE(candidates[i].encoded == createAdpcm2Bit(samples, shapings[i]));
        REQUIRE(candidates[i].perceptualNoise == perceptualNoise(samples, decode(candidates[i].encoded), kSampleRate));
        REQUIRE(candidates[i].drift == lowFrequencyDrift(samples, decode(candidates[i].encoded), kSampleRate));
    }

    const size_t best = bestShapingCandidate(candidates);
    REQUIRE(candidates[best].drift <= kMaxLowFrequencyDrift);
    for (const auto& candidate : candidates)
    {
        if (candidate.drift <= kMaxLowFrequencyDrift)
        {
            REQUIRE(candidates[best].perceptualNoise <= candidate.perceptualNoise);
        }
    }

    REQUIRE_THROWS(bestShapingCandidate({}));
}

TEST_CASE("Low-frequency drift")
{
    auto signal = tone(1000, 0.5, 4000);

    // identical signal and constant offset
    REQUIRE(lowFrequencyDrift(signal.reference, withNoise(signal, std::vector<int>(4000, 0)), kSampleRate) == 0.0);
    REQUIRE(std::abs(lowFrequencyDrift(signal.reference, withNoise(signal, std::vector<int>(4000, -3)), kSampleRate) - 3.0) < 1e-9);

    // audible noise is not drift, even if it is loud
    REQUIRE(lowFrequencyDrift(signal.reference, withNoise(signal, sineNoise(500, 20, 4000)), kSampleRate) < 0.5);
    REQUIRE(lowFrequencyDrift(signal.reference, withNoise(signal, sineNoise(100, 20, 4000)), kSampleRate) < 2.0);

    // a slow wander of the output is
    std::vector<int> wander(4000);
    for (size_t i = 0; i < wander.size(); ++i)
    {
        wander[i] = static_cast<int>(std::lround(20.0 * std::sin(2 * kPi * 2.0 * i / kSampleRate)));
    }
    REQUIRE(lowFrequencyDrift(signal.reference, withNoise(signal, wander), kSampleRate) > 15.0);
}

TEST_CASE("Automatic noise shaping does not select drifting candidates")
{
    auto candidate = [](double weight, double noise, double drift) {
        return ShapingCandidate{{NoiseShapingMode::fixed, weight}, {}, noise, drift};
    };

    // the least audible candidate drifts too much
    REQUIRE(bestShapingCandidate({candidate(0.5, 10.0, 0.0), candidate(-0.99, 5.0, 30.0), candidate(-0.5, 8.0, 4.0)}) == 2);
    REQUIRE(bestShapingCandidate({candidate(-0.99, 5.0, 30.0), candidate(0.5, 10.0, 0.0)}) == 1);

    // if all drift too much, the one with the least drift is used
    REQUIRE(bestShapingCandidate({candidate(-0.99, 5.0, 30.0), candidate(-0.9, 6.0, 10.0), candidate(-0.95, 5.5, 20.0)}) == 1);
}

TEST_CASE("Automatic noise shaping keeps the drift low on real audio")
{
    // With this file, -0.99 has the lowest perceptual noise, but drifts by about 50 levels.
    auto wave = loadWaveFileToMono(getTestDataDir() + "/jetpack.wav");
    const uint32_t sampleRate = 22050;
    const auto samples = resample(wave.data, wave.sampleRate, sampleRate);

    auto decode = [](const std::vector<uint8_t>& encoded) {
        return decodeAdpcm2(encoded.front(), std::vector<uint8_t>(encoded.begin() + 1, encoded.end()));
    };

    const auto candidates = evaluateShapings(samples, sampleRate, defaultShapingCandidates(), createAdpcm2Bit, decode);
    const auto& best = candidates[bestShapingCandidate(candidates)];
    REQUIRE(best.drift <= kMaxLowFrequencyDrift);
    REQUIRE(best.shaping.mode == NoiseShapingMode::fixed);
    REQUIRE(best.shaping.weight < 0.0);  // low-frequency shaping is still preferred

    const auto strongest = std::find_if(candidates.begin(), candidates.end(), [](const ShapingCandidate& c) {
        return c.shaping.mode == NoiseShapingMode::fixed && c.shaping.weight == -0.99;
    });
    REQUIRE(strongest != candidates.end());
    REQUIRE(strongest->drift > 4 * kMaxLowFrequencyDrift);
}

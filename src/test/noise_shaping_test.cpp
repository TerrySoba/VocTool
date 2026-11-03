#include "catch_importer.h"

#include "noise_shaping.h"
#include "encode_creative_adpcm.h"
#include "decode_creative_adpcm.h"
#include "resampling.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

std::vector<double> sineMix(const std::vector<double>& frequencies, double sampleRate, size_t length)
{
    std::vector<double> samples(length);
    for (size_t i = 0; i < length; ++i)
    {
        for (double frequency : frequencies)
        {
            samples[i] += 0.4 / frequencies.size() * 2 * std::sin(2 * kPi * frequency * i / sampleRate);
        }
    }
    return samples;
}

// error of decoded 8-bit output compared to the full precision input
std::vector<double> noise(const std::vector<double>& samples, const std::vector<uint8_t>& decoded)
{
    REQUIRE(decoded.size() >= samples.size());
    std::vector<double> result(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        result[i] = decoded[i] - std::clamp(samples[i] * 128.0 + 128.0, 0.0, 255.0);
    }
    return result;
}

double energy(const std::vector<double>& data)
{
    double sum = 0;
    for (double value : data) sum += value * value;
    return sum;
}

// ratio of noise energy below fs/4 to noise energy above fs/4, using simple 2-tap filters
double lowToHighRatio(const std::vector<double>& data)
{
    double low = 0, high = 0;
    for (size_t i = 1; i < data.size(); ++i)
    {
        low += (data[i] + data[i - 1]) * (data[i] + data[i - 1]);
        high += (data[i] - data[i - 1]) * (data[i] - data[i - 1]);
    }
    return low / high;
}

std::vector<uint8_t> decode4(std::vector<uint8_t> encoded)
{
    return decodeAdpcm4(encoded.front(), std::span<uint8_t>(encoded).subspan(1));
}

std::vector<uint8_t> decode2(const std::vector<uint8_t>& encoded)
{
    return decodeAdpcm2(encoded.front(), std::vector<uint8_t>(encoded.begin() + 1, encoded.end()));
}

std::vector<uint8_t> decode3(const std::vector<uint8_t>& encoded)
{
    return decodeAdpcm3(encoded.front(), std::vector<uint8_t>(encoded.begin() + 1, encoded.end()));
}

} // namespace

TEST_CASE("Noise shaping parameter parsing")
{
    REQUIRE(noiseShapingFromString("off").mode == NoiseShapingMode::off);
    REQUIRE(noiseShapingFromString("dynamic").mode == NoiseShapingMode::dynamic);
    REQUIRE(noiseShapingFromString("static").mode == NoiseShapingMode::fixed);
    REQUIRE(noiseShapingFromString("static").weight == 1.0);
    REQUIRE(noiseShapingFromString("0.5").mode == NoiseShapingMode::fixed);
    REQUIRE(noiseShapingFromString("0.5").weight == 0.5);
    REQUIRE(noiseShapingFromString("-0.25").weight == -0.25);
    REQUIRE(noiseShapingFromString("0").mode == NoiseShapingMode::off);
    REQUIRE_THROWS(noiseShapingFromString("1.5"));
    REQUIRE_THROWS(noiseShapingFromString("-1"));
    REQUIRE_THROWS(noiseShapingFromString("0.5x"));
    REQUIRE_THROWS(noiseShapingFromString("foo"));
}

TEST_CASE("Noise shaping weights")
{
    std::vector<int32_t> samples(1000);
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i] = static_cast<int32_t>(10000 * std::sin(2 * kPi * 0.01 * i));
    }

    REQUIRE(computeShapingWeights(samples, {NoiseShapingMode::off}).empty());

    auto fixed = computeShapingWeights(samples, {NoiseShapingMode::fixed, 0.5});
    REQUIRE(fixed.size() == samples.size());
    REQUIRE(std::all_of(fixed.begin(), fixed.end(), [](int16_t w) { return w == 512; }));

    // Dynamic shaping moves the noise to where the signal has its energy, so it is masked.
    // (A pure sine gives no shaping at all, because one of the bands is empty.)
    auto twoTone = [&](double strongFrequency, double weakFrequency) {
        for (size_t i = 0; i < samples.size(); ++i)
        {
            samples[i] = static_cast<int32_t>(
                10000 * std::sin(2 * kPi * strongFrequency * i) + 300 * std::sin(2 * kPi * weakFrequency * i));
        }
        return computeShapingWeights(samples, {NoiseShapingMode::dynamic});
    };

    SECTION("dynamic: high frequency signal gives high-pass shaping")
    {
        auto dynamic = twoTone(0.4, 0.1);
        REQUIRE(dynamic.size() == samples.size());
        REQUIRE(std::all_of(dynamic.begin() + 100, dynamic.end() - 100, [](int16_t w) { return w == 1024; }));
    }

    SECTION("dynamic: low frequency signal gives low-pass shaping")
    {
        auto dynamic = twoTone(0.1, 0.4);
        REQUIRE(std::all_of(dynamic.begin() + 100, dynamic.end() - 100, [](int16_t w) { return w == -512; }));
    }
}

TEST_CASE("Encoding with full precision input")
{
    auto samples = sineMix({440, 1300}, 11025, 3001);

    // the encoder minimizes the error to the full precision signal, so it must be at least
    // as good as encoding the signal rounded to 8 bit
    double fromSamples = energy(noise(samples, decode4(createAdpcm4Bit(samples))));
    double fromRaw = energy(noise(samples, decode4(createAdpcm4BitFromRaw(toUint8Vector(samples)))));
    REQUIRE(fromSamples <= fromRaw);

    REQUIRE(createAdpcm4Bit(samples).front() == toUint8Vector(samples).front());
}

TEST_CASE("Noise shaping moves noise in frequency")
{
    auto samples = sineMix({300, 700, 1100}, 11025, 6001);

    SECTION("ADPCM4")
    {
        auto flat = noise(samples, decode4(createAdpcm4Bit(samples, {NoiseShapingMode::off})));
        auto highPass = noise(samples, decode4(createAdpcm4Bit(samples, {NoiseShapingMode::fixed, 1.0})));
        auto lowPass = noise(samples, decode4(createAdpcm4Bit(samples, {NoiseShapingMode::fixed, -0.5})));

        // measured: high-pass about 0.49 times, low-pass about 2.1 times the flat ratio
        REQUIRE(lowToHighRatio(highPass) < 0.7 * lowToHighRatio(flat));
        REQUIRE(lowToHighRatio(lowPass) > 1.5 * lowToHighRatio(flat));

        // shaping increases total noise, but not dramatically
        REQUIRE(energy(highPass) > energy(flat));
        REQUIRE(energy(highPass) < 4.0 * energy(flat));
    }

    SECTION("ADPCM2")
    {
        auto flat = noise(samples, decode2(createAdpcm2Bit(samples, {NoiseShapingMode::off})));
        auto highPass = noise(samples, decode2(createAdpcm2Bit(samples, {NoiseShapingMode::fixed, 1.0})));

        // measured: about 0.57 times the flat ratio
        REQUIRE(lowToHighRatio(highPass) < 0.7 * lowToHighRatio(flat));
    }

    SECTION("ADPCM3")
    {
        auto flat = noise(samples, decode3(createAdpcm3Bit(samples, {NoiseShapingMode::off})));
        auto highPass = noise(samples, decode3(createAdpcm3Bit(samples, {NoiseShapingMode::fixed, 1.0})));
        auto lowPass = noise(samples, decode3(createAdpcm3Bit(samples, {NoiseShapingMode::fixed, -0.5})));

        REQUIRE(lowToHighRatio(highPass) < 0.7 * lowToHighRatio(flat));
        REQUIRE(lowToHighRatio(lowPass) > 1.5 * lowToHighRatio(flat));
        REQUIRE(energy(highPass) > energy(flat));
    }

    SECTION("dynamic")
    {
        auto encoded = createAdpcm4Bit(samples, {NoiseShapingMode::dynamic});
        REQUIRE(encoded.size() == 1 + (samples.size() - 1) / 2);
        REQUIRE(createAdpcm4Bit(samples, {NoiseShapingMode::dynamic}) == encoded);
    }
}

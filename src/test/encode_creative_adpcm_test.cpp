#include "catch_importer.h"

#include "encode_creative_adpcm.h"
#include "decode_creative_adpcm.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

std::vector<uint8_t> decode(std::vector<uint8_t> encoded)
{
    return decodeAdpcm4(encoded.front(), std::span<uint8_t>(encoded).subspan(1));
}

std::vector<uint8_t> decode2(const std::vector<uint8_t>& encoded)
{
    return decodeAdpcm2(encoded.front(), std::vector<uint8_t>(encoded.begin() + 1, encoded.end()));
}

uint64_t squaredError(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& decoded)
{
    REQUIRE(decoded.size() >= raw.size());
    uint64_t sum = 0;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        int64_t diff = (int64_t)raw[i] - (int64_t)decoded[i];
        sum += diff * diff;
    }
    return sum;
}

// The cost the encoder minimizes: squared error of all real samples plus, for the padding samples
// that fill up the last byte, the squared jump from the previous decoded value.
uint64_t encoderCostUnchecked(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& decoded)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        int64_t diff = (int64_t)raw[i] - (int64_t)decoded[i];
        sum += diff * diff;
    }
    for (size_t i = raw.size(); i < decoded.size(); ++i)
    {
        int64_t jump = (int64_t)decoded[i] - (int64_t)decoded[i - 1];
        sum += jump * jump;
    }
    return sum;
}

uint64_t encoderCost(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& decoded)
{
    REQUIRE(decoded.size() >= raw.size());
    return encoderCostUnchecked(raw, decoded);
}

// Tries every possible code sequence using the real decoder and returns the minimum encoderCost().
// symbolCount may be larger than raw.size() - 1 to include padding.
template <typename Decoder, typename DecodeFunction>
uint64_t bruteForceMinimumCost(const std::vector<uint8_t>& raw, size_t symbolCount, int symbolBits, DecodeFunction decodeSymbol)
{
    uint64_t best = std::numeric_limits<uint64_t>::max();
    std::vector<uint8_t> decoded(symbolCount + 1);
    decoded[0] = raw[0];
    for (uint64_t code = 0; code < (1ull << (symbolBits * symbolCount)); ++code)
    {
        Decoder decoder(raw[0]);
        for (size_t n = 0; n < symbolCount; ++n)
        {
            decoded[n + 1] = decodeSymbol(decoder, (code >> (symbolBits * n)) & ((1u << symbolBits) - 1));
        }
        best = std::min(best, encoderCostUnchecked(raw, decoded));
    }
    return best;
}

uint64_t bruteForceMinimumError(const std::vector<uint8_t>& raw, size_t symbolCount = 0)
{
    return bruteForceMinimumCost<CreativeAdpcmDecoder4Bit>(raw, symbolCount ? symbolCount : raw.size() - 1, 4,
        [](CreativeAdpcmDecoder4Bit& decoder, uint8_t symbol) { return decoder.decodeNibble(symbol); });
}

uint64_t bruteForceMinimumError2(const std::vector<uint8_t>& raw, size_t symbolCount = 0)
{
    return bruteForceMinimumCost<CreativeAdpcmDecoder2Bit>(raw, symbolCount ? symbolCount : raw.size() - 1, 2,
        [](CreativeAdpcmDecoder2Bit& decoder, uint8_t symbol) { return decoder.decode2bits(symbol); });
}

std::vector<uint8_t> testSignal(size_t length)
{
    std::vector<uint8_t> raw(length);
    for (size_t i = 0; i < raw.size(); ++i)
    {
        double t = (double)i / 11025.0;
        raw[i] = (uint8_t)std::lround(128.0 + 60.0 * std::sin(2 * 3.14159265358979 * 440.0 * t) + 30.0 * std::sin(2 * 3.14159265358979 * 1300.0 * t));
    }
    return raw;
}

} // namespace

TEST_CASE("ADPCM4 Viterbi encoder finds the optimal encoding")
{
    std::mt19937 gen(1234);
    std::uniform_int_distribution<int> anyValue(0, 255);
    std::uniform_int_distribution<int> smallStep(-12, 12);

    for (int run = 0; run < 20; ++run)
    {
        // 1 reference byte + 4 nibbles = 65536 combinations for brute force
        std::vector<uint8_t> raw(5);
        raw[0] = anyValue(gen);
        for (size_t i = 1; i < raw.size(); ++i)
        {
            // mix of jumps that the decoder cannot follow and small steps it can
            raw[i] = (run % 2) ? anyValue(gen) : std::clamp(raw[i - 1] + smallStep(gen), 0, 255);
        }

        auto encoded = createAdpcm4BitFromRaw(raw);
        REQUIRE(encoded.size() == 3);
        REQUIRE(squaredError(raw, decode(encoded)) == bruteForceMinimumError(raw));
    }
}

TEST_CASE("ADPCM4 Viterbi encoder handles long input")
{
    // longer than the internal traceback window, to test the sliding window
    std::vector<uint8_t> raw = testSignal(5001);

    auto encoded = createAdpcm4BitFromRaw(raw);
    REQUIRE(encoded.size() == 1 + (raw.size() - 1) / 2);

    auto decoded = decode(encoded);
    REQUIRE(decoded.size() == raw.size());

    double meanSquaredError = (double)squaredError(raw, decoded) / raw.size();
    REQUIRE(meanSquaredError < 4.0);

    // encoding must be deterministic
    REQUIRE(createAdpcm4BitFromRaw(raw) == encoded);
}

TEST_CASE("ADPCM4 Viterbi encoder edge cases")
{
    SECTION("single sample")
    {
        std::vector<uint8_t> raw = {200};
        REQUIRE(createAdpcm4BitFromRaw(raw) == std::vector<uint8_t>{200});
    }

    SECTION("odd number of nibbles keeps all samples")
    {
        std::vector<uint8_t> raw = {128, 130, 135, 140};  // 3 nibbles, padded to 4
        auto encoded = createAdpcm4BitFromRaw(raw);
        REQUIRE(encoded.size() == 3);
        auto decoded = decode(encoded);
        REQUIRE(decoded.size() == 5);
        REQUIRE(encoderCost(raw, decoded) == bruteForceMinimumError(raw, 4));
    }

    SECTION("padding holds the last value, so there is no click at the end")
    {
        std::vector<uint8_t> raw = testSignal(1000);
        raw.insert(raw.end(), 100, 128);  // sound ends in silence, 1099 nibbles => 1 padding sample
        auto decoded = decode(createAdpcm4BitFromRaw(raw));
        REQUIRE(decoded.size() == raw.size() + 1);
        REQUIRE(decoded.back() == 128);
    }

    SECTION("empty input throws")
    {
        REQUIRE_THROWS(createAdpcm4BitFromRaw({}));
    }
}

TEST_CASE("ADPCM2 Viterbi encoder finds the optimal encoding")
{
    std::mt19937 gen(4321);
    std::uniform_int_distribution<int> anyValue(0, 255);
    std::uniform_int_distribution<int> smallStep(-12, 12);

    for (int run = 0; run < 20; ++run)
    {
        // 1 reference byte + 8 symbols = 65536 combinations for brute force
        std::vector<uint8_t> raw(9);
        raw[0] = anyValue(gen);
        for (size_t i = 1; i < raw.size(); ++i)
        {
            raw[i] = (run % 2) ? anyValue(gen) : std::clamp(raw[i - 1] + smallStep(gen), 0, 255);
        }

        auto encoded = createAdpcm2BitFromRaw(raw);
        REQUIRE(encoded.size() == 3);
        REQUIRE(squaredError(raw, decode2(encoded)) == bruteForceMinimumError2(raw));
    }
}

TEST_CASE("ADPCM2 Viterbi encoder handles long input")
{
    std::vector<uint8_t> raw = testSignal(5001);

    auto encoded = createAdpcm2BitFromRaw(raw);
    REQUIRE(encoded.size() == 1 + (raw.size() - 1) / 4);

    auto decoded = decode2(encoded);
    REQUIRE(decoded.size() == raw.size());

    // the optimum for this signal is about 41.7, the previous block search encoder reached 80.6 at best
    double meanSquaredError = (double)squaredError(raw, decoded) / raw.size();
    REQUIRE(meanSquaredError < 50.0);

    REQUIRE(createAdpcm2BitFromRaw(raw) == encoded);
}

TEST_CASE("ADPCM2 Viterbi encoder edge cases")
{
    SECTION("single sample")
    {
        std::vector<uint8_t> raw = {50};
        REQUIRE(createAdpcm2BitFromRaw(raw) == std::vector<uint8_t>{50});
    }

    SECTION("incomplete last byte keeps all samples")
    {
        std::vector<uint8_t> raw = {128, 129, 131, 134, 138, 143};  // 5 symbols, padded to 8
        auto encoded = createAdpcm2BitFromRaw(raw);
        REQUIRE(encoded.size() == 3);
        auto decoded = decode2(encoded);
        REQUIRE(decoded.size() == 9);
        REQUIRE(encoderCost(raw, decoded) == bruteForceMinimumError2(raw, 8));
    }

    SECTION("padding holds the last value, so there is no click at the end")
    {
        std::vector<uint8_t> raw = testSignal(1002);
        raw.insert(raw.end(), 100, 128);  // sound ends in silence, 1101 symbols => 3 padding samples
        auto decoded = decode2(createAdpcm2BitFromRaw(raw));
        REQUIRE(decoded.size() == raw.size() + 3);
        REQUIRE(std::all_of(decoded.end() - 4, decoded.end(), [](uint8_t v) { return v == 128; }));
    }

    SECTION("empty input throws")
    {
        REQUIRE_THROWS(createAdpcm2BitFromRaw({}));
    }
}

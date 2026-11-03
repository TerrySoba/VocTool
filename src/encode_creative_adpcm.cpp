#include "encode_creative_adpcm.h"
#include "noise_shaping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

/*
 * Both Creative ADPCM decoders have a very small state: the previous output value (0..255)
 * plus a step size. This makes it possible to find the optimal encoding (minimum squared
 * error between input and decoded output) with the Viterbi algorithm over all decoder states.
 *
 * Each codec description below provides:
 *   kStateCount     number of decoder states
 *   kSymbolBits     bits of a decoder symbol
 *   kSymbolsPerByte number of symbols packed into one byte
 *   storedBits      bits actually stored for the symbol at a position in the byte. If this is less
 *                   than kSymbolBits, the stored bits are the upper bits of the decoder symbol and
 *                   the lower bits are always zero (the decoder shifts them in).
 *   initialState    decoder state after the reference byte
 *   previousValue   last decoded output value of a state
 *   step            decoder transition for (state, symbol), must match decode_creative_adpcm.h
 */

namespace {

/**
 * 4-bit ADPCM, see CreativeAdpcmDecoder4Bit.
 * State: previousValue * 4 + log2(accumulator), accumulator is 1, 2, 4 or 8.
 */
struct Adpcm4Codec
{
    static constexpr int kStateCount = 256 * 4;
    static constexpr int kSymbolBits = 4;
    static constexpr int kSymbolsPerByte = 2;

    static constexpr int storedBits(int /*positionInByte*/)
    {
        return 4;
    }

    static int initialState(uint8_t first)
    {
        return first * 4;  // accumulator starts at 1
    }

    static int previousValue(int state)
    {
        return state >> 2;
    }

    static void step(int state, int symbol, int& nextState, int& value)
    {
        const int previous = state >> 2;
        const int accumulator = 1 << (state & 3);
        const int data = symbol & 7;
        const int sign = (symbol & 8) ? 1 : -1;
        value = std::clamp(previous + sign * (data * accumulator + accumulator / 2), 0, 255);

        int newAccumulator = accumulator;
        if (data == 0 && newAccumulator > 1) newAccumulator /= 2;
        if (data >= 5 && newAccumulator < 8) newAccumulator *= 2;
        const int log2Accumulator = (newAccumulator == 1) ? 0 : (newAccumulator == 2) ? 1 : (newAccumulator == 4) ? 2 : 3;

        nextState = value * 4 + log2Accumulator;
    }
};

/**
 * 2-bit ADPCM, see CreativeAdpcmDecoder2Bit.
 * The scale is always one of 0, 4, 8, 12, 16, 20.
 * State: previousValue * 6 + scale / 4
 */
struct Adpcm2Codec
{
    static constexpr int kScaleCount = 6;
    static constexpr int kStateCount = 256 * kScaleCount;
    static constexpr int kSymbolBits = 2;
    static constexpr int kSymbolsPerByte = 4;

    static constexpr int storedBits(int /*positionInByte*/)
    {
        return 2;
    }

    static int initialState(uint8_t first)
    {
        return first * kScaleCount;  // scale starts at 0
    }

    static int previousValue(int state)
    {
        return state / kScaleCount;
    }

    static void step(int state, int symbol, int& nextState, int& value)
    {
        static const int8_t scaleMap[24] = {
            0, 1, 0, -1, 1, 3, -1, -3,
            2, 6, -2, -6, 4, 12, -4, -12,
            8, 24, -8, -24, 16, 48, -16, -48};
        static const int8_t adjustMap[24] = {
            0, 4, 0, 4,
            -4, 4, -4, 4, -4, 4, -4, 4,
            -4, 4, -4, 4, -4, 4, -4, 4,
            -4, 0, -4, 0};

        const int previous = state / kScaleCount;
        const int scale = (state % kScaleCount) * 4;
        const int index = std::clamp(symbol + scale, 0, 23);
        value = std::clamp(previous + scaleMap[index], 0, 255);
        const int newScale = scale + adjustMap[index];
        if (newScale < 0 || newScale > 4 * (kScaleCount - 1))
        {
            throw std::logic_error("ADPCM2 scale out of range");
        }

        nextState = value * kScaleCount + newScale / 4;
    }
};

/**
 * 2.6-bit ADPCM, see CreativeAdpcmDecoder3Bit.
 * A byte holds three symbols: 3 bits, 3 bits and 2 bits. The decoder shifts the last one left
 * by one bit, so it can only be an even 3-bit symbol.
 * The scale is always one of 0, 8, 16, 24, 32.
 * State: previousValue * 5 + scale / 8
 */
struct Adpcm3Codec
{
    static constexpr int kScaleCount = 5;
    static constexpr int kStateCount = 256 * kScaleCount;
    static constexpr int kSymbolBits = 3;
    static constexpr int kSymbolsPerByte = 3;

    static constexpr int storedBits(int positionInByte)
    {
        return (positionInByte == 2) ? 2 : 3;
    }

    static int initialState(uint8_t first)
    {
        return first * kScaleCount;  // scale starts at 0
    }

    static int previousValue(int state)
    {
        return state / kScaleCount;
    }

    static void step(int state, int symbol, int& nextState, int& value)
    {
        static const int8_t scaleMap[40] = {
            0,  1,  2,  3,  0,  -1,  -2,  -3,
            1,  3,  5,  7, -1,  -3,  -5,  -7,
            2,  6, 10, 14, -2,  -6, -10, -14,
            4, 12, 20, 28, -4, -12, -20, -28,
            5, 15, 25, 35, -5, -15, -25, -35};
        static const int8_t adjustMap[40] = {
             0, 0, 0, 8,  0, 0, 0, 8,
            -8, 0, 0, 8, -8, 0, 0, 8,
            -8, 0, 0, 8, -8, 0, 0, 8,
            -8, 0, 0, 8, -8, 0, 0, 8,
            -8, 0, 0, 0, -8, 0, 0, 0};

        const int previous = state / kScaleCount;
        const int scale = (state % kScaleCount) * 8;
        const int index = std::clamp(symbol + scale, 0, 39);
        value = std::clamp(previous + scaleMap[index], 0, 255);
        const int newScale = scale + adjustMap[index];
        if (newScale < 0 || newScale > 8 * (kScaleCount - 1))
        {
            throw std::logic_error("ADPCM3 scale out of range");
        }

        nextState = value * kScaleCount + newScale / 8;
    }
};

/** Precomputed next state and decoded output value for every (state, symbol) pair. */
template <typename Codec>
struct TransitionTable
{
    static constexpr int kSymbolCount = 1 << Codec::kSymbolBits;

    std::array<uint16_t, Codec::kStateCount * kSymbolCount> nextState;
    std::array<uint8_t, Codec::kStateCount * kSymbolCount> value;

    TransitionTable()
    {
        for (int state = 0; state < Codec::kStateCount; ++state)
        {
            for (int symbol = 0; symbol < kSymbolCount; ++symbol)
            {
                int next, decoded;
                Codec::step(state, symbol, next, decoded);
                if (next < 0 || next >= Codec::kStateCount)
                {
                    throw std::logic_error("ADPCM state out of range");
                }
                nextState[state * kSymbolCount + symbol] = static_cast<uint16_t>(next);
                value[state * kSymbolCount + symbol] = static_cast<uint8_t>(decoded);
            }
        }
    }
};

template <typename Codec>
const TransitionTable<Codec>& transitionTable()
{
    static const TransitionTable<Codec> table;
    return table;
}

/** Converts an 8-bit unsigned decoder value to the signed 16-bit scale used by the encoder. */
inline int32_t toSigned16(int value)
{
    return (value - 128) * 256;
}

/**
 * Viterbi encoder. The reference byte is stored as is, every target sample becomes one symbol.
 * Symbols are packed into bytes, first symbol in the most significant bits.
 *
 * @param reference      First sample, stored verbatim. Initial state of the decoder.
 * @param targets        Following samples in signed 16-bit scale (see toSigned16()), so samples can
 *                       have more precision than the 8-bit output.
 * @param shapingWeights Noise shaping weight per target sample in units of 1/1024, or empty for no
 *                       noise shaping. See noise_shaping.h.
 *
 * Without noise shaping the result is the encoding with minimum squared error.
 *
 * With noise shaping the target of each sample is modified by feeding back the error of the previous
 * sample, exactly like ADPCM-XQ does. As the error depends on the path through the trellis, every
 * state keeps the error of its survivor path ("per-survivor processing"). The squared difference to
 * the shaped target is minimized, which is the same criterion ADPCM-XQ uses in its lookahead search.
 */
template <typename Codec>
std::vector<uint8_t> viterbiEncode(uint8_t reference, const std::vector<int32_t>& targets, const std::vector<int16_t>& shapingWeights)
{
    constexpr int kStateCount = Codec::kStateCount;
    constexpr int kSymbolBits = Codec::kSymbolBits;
    constexpr int kSymbolCount = 1 << kSymbolBits;
    constexpr int kSymbolsPerByte = Codec::kSymbolsPerByte;
    static_assert(kStateCount << kSymbolBits <= std::numeric_limits<uint16_t>::max() + 1, "back pointer does not fit into 16 bit");

    if (!shapingWeights.empty() && shapingWeights.size() != targets.size())
    {
        throw std::logic_error("Number of shaping weights does not match number of samples");
    }

    // Pad to full bytes, so no sample is lost. The padding samples are played back by the decoder too,
    // so they should hold the last value: their cost is the jump from the previous decoded value.
    // Otherwise a random padding code could produce an audible click at the end.
    const size_t sampleCount = targets.size();
    const size_t paddedCount = (sampleCount + kSymbolsPerByte - 1) / kSymbolsPerByte * kSymbolsPerByte;

    std::vector<uint8_t> symbols;
    symbols.reserve(paddedCount);

    if (paddedCount > 0)
    {
        const auto& transitions = transitionTable<Codec>();
        constexpr uint64_t kInfinity = std::numeric_limits<uint64_t>::max();

        /*
         * Keeping back pointers for the whole input would need several KiB per sample. Instead a
         * sliding window is used: whenever the window is full, the oldest kCommitLength decisions of
         * the currently best path are written out. Survivor paths practically always merge within a
         * few dozen samples, so this gives the same result as a full traceback. To keep the emitted
         * symbols a consistent path, all states that do not descend from the committed state are
         * discarded afterwards.
         */
        constexpr size_t kCommitLength = 256;
        constexpr size_t kWindowLength = 2 * kCommitLength;

        // back pointer: (previousState << kSymbolBits) | symbol
        auto backPointerState = [](uint16_t backPointer) { return backPointer >> kSymbolBits; };
        auto backPointerSymbol = [](uint16_t backPointer) { return static_cast<uint8_t>(backPointer & (kSymbolCount - 1)); };

        std::vector<uint64_t> cost(kStateCount, kInfinity);
        std::vector<uint64_t> nextCost(kStateCount);
        std::vector<int32_t> shapingError(kStateCount, 0);  // noise shaping error of each survivor path
        std::vector<int32_t> nextShapingError(kStateCount, 0);
        std::vector<uint16_t> backPointers(kWindowLength * kStateCount);
        size_t windowFill = 0;  // number of steps stored in backPointers

        cost[Codec::initialState(reference)] = 0;

        auto ancestorOf = [&](int state, size_t step) {
            for (size_t s = windowFill; s-- > step;)
            {
                state = backPointerState(backPointers[s * kStateCount + state]);
            }
            return state;
        };

        auto commit = [&](size_t count) {
            const int best = static_cast<int>(std::min_element(cost.begin(), cost.end()) - cost.begin());

            // trace back best path through the whole window
            std::vector<uint8_t> path(windowFill);
            int state = best;
            for (size_t s = windowFill; s-- > 0;)
            {
                uint16_t backPointer = backPointers[s * kStateCount + state];
                path[s] = backPointerSymbol(backPointer);
                state = backPointerState(backPointer);
            }
            symbols.insert(symbols.end(), path.begin(), path.begin() + count);

            // discard states that do not descend from the committed state
            const int committedState = ancestorOf(best, count);
            for (int s = 0; s < kStateCount; ++s)
            {
                if (cost[s] != kInfinity && ancestorOf(s, count) != committedState)
                {
                    cost[s] = kInfinity;
                }
            }

            // drop committed steps from window
            std::memmove(
                backPointers.data(),
                backPointers.data() + count * kStateCount,
                (windowFill - count) * kStateCount * sizeof(uint16_t));
            windowFill -= count;
        };

        for (size_t position = 0; position < paddedCount; ++position)
        {
            const bool isPadding = position >= sampleCount;
            const int32_t sample = isPadding ? 0 : targets[position];
            const int64_t weight = (isPadding || shapingWeights.empty()) ? 0 : shapingWeights[position];
            // symbols with fewer stored bits must have their lower bits zero
            const int symbolIncrement = 1 << (kSymbolBits - Codec::storedBits(static_cast<int>(position % kSymbolsPerByte)));

            std::fill(nextCost.begin(), nextCost.end(), kInfinity);
            uint16_t* stepBackPointers = &backPointers[windowFill * kStateCount];

            for (int state = 0; state < kStateCount; ++state)
            {
                const uint64_t stateCost = cost[state];
                if (stateCost == kInfinity) continue;

                // noise shaping, same as noise_shape() in ADPCM-XQ
                int32_t target = sample;
                int32_t errorBase = -sample;  // the decoded value gets added to this
                if (weight != 0)
                {
                    const int32_t error = shapingError[state];
                    int32_t feedback = static_cast<int32_t>(-((weight * error + 512) >> 10));
                    if (weight < 0 && feedback != 0)
                    {
                        if (feedback == error)
                        {
                            feedback += (feedback < 0) ? 1 : -1;
                        }
                        target = sample + feedback;
                        errorBase = -sample;
                    }
                    else
                    {
                        target = sample + feedback;
                        errorBase = -target;
                    }
                }

                const int base = state * kSymbolCount;
                for (int symbol = 0; symbol < kSymbolCount; symbol += symbolIncrement)
                {
                    const int next = transitions.nextState[base + symbol];
                    const int32_t decoded = toSigned16(transitions.value[base + symbol]);
                    const int64_t difference = isPadding
                        ? static_cast<int64_t>(decoded) - toSigned16(Codec::previousValue(state))
                        : static_cast<int64_t>(decoded) - target;
                    const uint64_t newCost = stateCost + static_cast<uint64_t>(difference * difference);
                    if (newCost < nextCost[next])
                    {
                        nextCost[next] = newCost;
                        nextShapingError[next] = errorBase + decoded;
                        stepBackPointers[next] = static_cast<uint16_t>((state << kSymbolBits) | symbol);
                    }
                }
            }

            cost.swap(nextCost);
            shapingError.swap(nextShapingError);
            ++windowFill;

            if (windowFill == kWindowLength)
            {
                commit(kCommitLength);
            }
        }

        commit(windowFill);
    }

    std::vector<uint8_t> binaryResult;
    binaryResult.reserve(symbols.size() / kSymbolsPerByte + 1);
    binaryResult.push_back(reference);
    for (size_t n = 0; n < symbols.size(); n += kSymbolsPerByte)
    {
        uint8_t byte = 0;
        for (int i = 0; i < kSymbolsPerByte; ++i)
        {
            const int bits = Codec::storedBits(i);
            byte = static_cast<uint8_t>((byte << bits) | (symbols[n + i] >> (kSymbolBits - bits)));
        }
        binaryResult.push_back(byte);
    }

    return binaryResult;
}

template <typename Codec>
std::vector<uint8_t> encodeRaw(const std::vector<uint8_t>& raw)
{
    if (raw.empty())
    {
        throw std::runtime_error("Cannot encode empty sample data");
    }

    std::vector<int32_t> targets;
    targets.reserve(raw.size() - 1);
    for (size_t i = 1; i < raw.size(); ++i)
    {
        targets.push_back(toSigned16(raw[i]));
    }
    return viterbiEncode<Codec>(raw[0], targets, {});
}

template <typename Codec>
std::vector<uint8_t> encodeSamples(const std::vector<double>& samples, const NoiseShaping& shaping)
{
    if (samples.empty())
    {
        throw std::runtime_error("Cannot encode empty sample data");
    }

    // same mapping as toUint8Vector(): -1.0 .. 1.0 => 0 .. 255
    auto toUnsigned8 = [](double sample) { return std::clamp(sample * 128.0 + 128.0, 0.0, 255.0); };

    const uint8_t reference = static_cast<uint8_t>(std::lround(toUnsigned8(samples.front())));

    std::vector<int32_t> targets;
    targets.reserve(samples.size() - 1);
    for (size_t i = 1; i < samples.size(); ++i)
    {
        targets.push_back(static_cast<int32_t>(std::lround((toUnsigned8(samples[i]) - 128.0) * 256.0)));
    }

    return viterbiEncode<Codec>(reference, targets, computeShapingWeights(targets, shaping));
}

} // namespace


std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw)
{
    return encodeRaw<Adpcm4Codec>(raw);
}

std::vector<uint8_t> createAdpcm2BitFromRaw(const std::vector<uint8_t>& raw)
{
    return encodeRaw<Adpcm2Codec>(raw);
}

std::vector<uint8_t> createAdpcm3BitFromRaw(const std::vector<uint8_t>& raw)
{
    return encodeRaw<Adpcm3Codec>(raw);
}

std::vector<uint8_t> createAdpcm4Bit(const std::vector<double>& samples, const NoiseShaping& shaping)
{
    return encodeSamples<Adpcm4Codec>(samples, shaping);
}

std::vector<uint8_t> createAdpcm2Bit(const std::vector<double>& samples, const NoiseShaping& shaping)
{
    return encodeSamples<Adpcm2Codec>(samples, shaping);
}

std::vector<uint8_t> createAdpcm3Bit(const std::vector<double>& samples, const NoiseShaping& shaping)
{
    return encodeSamples<Adpcm3Codec>(samples, shaping);
}

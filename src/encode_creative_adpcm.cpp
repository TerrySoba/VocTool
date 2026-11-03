#include "encode_creative_adpcm.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

/*
 * Both Creative ADPCM decoders have a very small state: the previous output value (0..255)
 * plus a step size. This makes it possible to find the optimal encoding (minimum squared
 * error between input and decoded output) with the Viterbi algorithm over all decoder states.
 *
 * Each codec description below provides:
 *   kStateCount   number of decoder states
 *   kSymbolBits   bits per encoded symbol
 *   initialState  decoder state after the reference byte
 *   step          decoder transition for (state, symbol), must match decode_creative_adpcm.h
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

    static int initialState(uint8_t first)
    {
        return first * 4;  // accumulator starts at 1
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

    static int initialState(uint8_t first)
    {
        return first * kScaleCount;  // scale starts at 0
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

/**
 * Viterbi encoder. The first sample is stored as reference byte, every following sample
 * becomes one symbol. Symbols are packed into bytes, first symbol in the most significant bits.
 */
template <typename Codec>
std::vector<uint8_t> viterbiEncode(const std::vector<uint8_t>& raw)
{
    constexpr int kStateCount = Codec::kStateCount;
    constexpr int kSymbolBits = Codec::kSymbolBits;
    constexpr int kSymbolCount = 1 << kSymbolBits;
    constexpr int kSymbolsPerByte = 8 / kSymbolBits;
    static_assert(kStateCount << kSymbolBits <= std::numeric_limits<uint16_t>::max() + 1, "back pointer does not fit into 16 bit");

    if (raw.empty())
    {
        throw std::runtime_error("Cannot encode empty sample data");
    }

    // Pad to full bytes, so no sample is lost. Padding samples do not contribute to the error,
    // so they cannot degrade the encoding of the real samples.
    const size_t sampleCount = raw.size() - 1;
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
        std::vector<uint16_t> backPointers(kWindowLength * kStateCount);
        std::array<uint64_t, 256> squaredError;
        size_t windowFill = 0;  // number of steps stored in backPointers

        cost[Codec::initialState(raw[0])] = 0;

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
            if (position < sampleCount)
            {
                const int target = raw[position + 1];
                for (int value = 0; value < 256; ++value)
                {
                    int64_t diff = value - target;
                    squaredError[value] = static_cast<uint64_t>(diff * diff);
                }
            }
            else
            {
                squaredError.fill(0);
            }

            std::fill(nextCost.begin(), nextCost.end(), kInfinity);
            uint16_t* stepBackPointers = &backPointers[windowFill * kStateCount];

            for (int state = 0; state < kStateCount; ++state)
            {
                const uint64_t stateCost = cost[state];
                if (stateCost == kInfinity) continue;

                const int base = state * kSymbolCount;
                for (int symbol = 0; symbol < kSymbolCount; ++symbol)
                {
                    const int next = transitions.nextState[base + symbol];
                    const uint64_t newCost = stateCost + squaredError[transitions.value[base + symbol]];
                    if (newCost < nextCost[next])
                    {
                        nextCost[next] = newCost;
                        stepBackPointers[next] = static_cast<uint16_t>((state << kSymbolBits) | symbol);
                    }
                }
            }

            cost.swap(nextCost);
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
    binaryResult.push_back(raw[0]);
    for (size_t n = 0; n < symbols.size(); n += kSymbolsPerByte)
    {
        uint8_t byte = 0;
        for (int i = 0; i < kSymbolsPerByte; ++i)
        {
            byte = static_cast<uint8_t>((byte << kSymbolBits) | symbols[n + i]);
        }
        binaryResult.push_back(byte);
    }

    return binaryResult;
}

} // namespace


std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw)
{
    return viterbiEncode<Adpcm4Codec>(raw);
}

std::vector<uint8_t> createAdpcm2BitFromRaw(const std::vector<uint8_t>& raw)
{
    return viterbiEncode<Adpcm2Codec>(raw);
}

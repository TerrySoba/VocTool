#include "encode_creative_adpcm.h"
#include "decode_creative_adpcm.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

uint64_t getNth2bit(int n, uint64_t value)
{
    return (0x3 & (value >> (2 * n)));
}

constexpr uint64_t constPow(uint64_t val, uint64_t exp)
{
    uint64_t res = 1;
    while (exp-- > 0)
    {
        res *= val;
    }

    return res;
}

/*
 * The complete state of the Creative 4-bit ADPCM decoder is the previous output value (0..255)
 * and the accumulator (1, 2, 4 or 8). This gives only 1024 possible states, which makes it
 * possible to find the optimal encoding (minimum squared error) with the Viterbi algorithm.
 *
 * State index: previousValue * 4 + log2(accumulator)
 */
constexpr int kStateCount = 256 * 4;
constexpr int kNibbleCount = 16;

/** Transition table: next state and decoded output value for every (state, nibble) pair. */
struct Adpcm4Transitions
{
    std::array<uint16_t, kStateCount * kNibbleCount> nextState;
    std::array<uint8_t, kStateCount * kNibbleCount> value;

    Adpcm4Transitions()
    {
        for (int state = 0; state < kStateCount; ++state)
        {
            const int previous = state >> 2;
            const int accumulator = 1 << (state & 3);
            for (int nibble = 0; nibble < kNibbleCount; ++nibble)
            {
                // must match CreativeAdpcmDecoder4Bit::decodeNibble()
                const int data = nibble & 7;
                const int sign = (nibble & 8) ? 1 : -1;
                const int decoded = std::clamp(previous + sign * (data * accumulator + accumulator / 2), 0, 255);
                int newAccumulator = accumulator;
                if (data == 0 && newAccumulator > 1) newAccumulator /= 2;
                if (data >= 5 && newAccumulator < 8) newAccumulator *= 2;
                const int log2Accumulator = (newAccumulator == 1) ? 0 : (newAccumulator == 2) ? 1 : (newAccumulator == 4) ? 2 : 3;

                nextState[state * kNibbleCount + nibble] = static_cast<uint16_t>(decoded * 4 + log2Accumulator);
                value[state * kNibbleCount + nibble] = static_cast<uint8_t>(decoded);
            }
        }
    }
};

const Adpcm4Transitions& adpcm4Transitions()
{
    static const Adpcm4Transitions transitions;
    return transitions;
}

// Back pointer: (previousState << 4) | nibble
inline int backPointerState(uint16_t backPointer) { return backPointer >> 4; }
inline uint8_t backPointerNibble(uint16_t backPointer) { return backPointer & 0xf; }

} // namespace


std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw)
{
    if (raw.empty())
    {
        throw std::runtime_error("Cannot encode empty sample data");
    }

    // The first sample is stored as reference byte, all following samples are encoded as nibbles.
    // Two nibbles form one byte, so pad an odd number of samples by repeating the last sample.
    std::vector<uint8_t> targets(raw.begin() + 1, raw.end());
    if (targets.size() % 2 != 0)
    {
        targets.push_back(raw.back());
    }

    std::vector<uint8_t> nibbles;
    nibbles.reserve(targets.size());

    if (!targets.empty())
    {
        const auto& transitions = adpcm4Transitions();
        constexpr uint64_t kInfinity = std::numeric_limits<uint64_t>::max();

        /*
         * Keeping back pointers for the whole input would need 2 KiB per sample. Instead a sliding
         * window is used: whenever the window is full, the oldest kCommitLength decisions of the
         * currently best path are written out. Survivor paths practically always merge within a few
         * dozen samples, so this gives the same result as a full traceback. To keep the emitted
         * nibbles a consistent path, all states that do not descend from the committed state are
         * discarded afterwards.
         */
        constexpr size_t kCommitLength = 256;
        constexpr size_t kWindowLength = 2 * kCommitLength;

        std::vector<uint64_t> cost(kStateCount, kInfinity);
        std::vector<uint64_t> nextCost(kStateCount);
        std::vector<uint16_t> backPointers(kWindowLength * kStateCount);
        std::array<uint64_t, 256> squaredError;
        size_t windowFill = 0;  // number of steps stored in backPointers

        cost[raw[0] * 4] = 0;  // decoder starts with the reference byte and accumulator 1

        auto bestState = [&]() {
            return static_cast<int>(std::min_element(cost.begin(), cost.end()) - cost.begin());
        };

        // Returns the nibbles of the path ending in `state`, covering all steps in the window.
        auto traceBack = [&](int state) {
            std::vector<uint8_t> path(windowFill);
            for (size_t step = windowFill; step-- > 0;)
            {
                uint16_t backPointer = backPointers[step * kStateCount + state];
                path[step] = backPointerNibble(backPointer);
                state = backPointerState(backPointer);
            }
            return path;
        };

        auto commit = [&](size_t count) {
            // follow best path back to the end of the committed part
            int state = bestState();
            std::vector<uint8_t> path = traceBack(state);
            nibbles.insert(nibbles.end(), path.begin(), path.begin() + count);

            int committedState = state;
            for (size_t step = windowFill; step-- > count;)
            {
                committedState = backPointerState(backPointers[step * kStateCount + committedState]);
            }

            // discard states that do not descend from the committed state
            for (int s = 0; s < kStateCount; ++s)
            {
                if (cost[s] == kInfinity) continue;
                int ancestor = s;
                for (size_t step = windowFill; step-- > count;)
                {
                    ancestor = backPointerState(backPointers[step * kStateCount + ancestor]);
                }
                if (ancestor != committedState)
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

        for (uint8_t target : targets)
        {
            for (int value = 0; value < 256; ++value)
            {
                int64_t diff = value - static_cast<int>(target);
                squaredError[value] = static_cast<uint64_t>(diff * diff);
            }

            std::fill(nextCost.begin(), nextCost.end(), kInfinity);
            uint16_t* stepBackPointers = &backPointers[windowFill * kStateCount];

            for (int state = 0; state < kStateCount; ++state)
            {
                const uint64_t stateCost = cost[state];
                if (stateCost == kInfinity) continue;

                const int base = state * kNibbleCount;
                for (int nibble = 0; nibble < kNibbleCount; ++nibble)
                {
                    const int next = transitions.nextState[base + nibble];
                    const uint64_t newCost = stateCost + squaredError[transitions.value[base + nibble]];
                    if (newCost < nextCost[next])
                    {
                        nextCost[next] = newCost;
                        stepBackPointers[next] = static_cast<uint16_t>((state << 4) | nibble);
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
    binaryResult.reserve(nibbles.size() / 2 + 1);
    binaryResult.push_back(raw[0]);
    for (size_t n = 0; n + 1 < nibbles.size(); n += 2)
    {
        binaryResult.push_back(static_cast<uint8_t>((nibbles[n] << 4) | nibbles[n + 1]));
    }

    return binaryResult;
}


struct Best2bit
{
    uint64_t bestIndex = 0;
    uint64_t bestDiff = std::numeric_limits<uint64_t>::max();
    CreativeAdpcmDecoder2Bit bestDecoder = CreativeAdpcmDecoder2Bit(0);
};

std::vector<uint8_t> createAdpcm2BitFromRaw(const std::vector<uint8_t>& raw, uint64_t combinedSamples)
{
    uint64_t squaredSum = 0u;

    CreativeAdpcmDecoder2Bit decoder(raw[0]);
    
    std::vector<uint64_t> result(raw.size() / combinedSamples);

    for (size_t i = 1; i < raw.size() / combinedSamples; ++i)
    {
        Best2bit bestResults;

        // try every possible input for the decoder
        for (uint64_t n = 0; n < constPow(4, combinedSamples); ++n)
        {
            CreativeAdpcmDecoder2Bit decoderCopy = decoder;
            uint64_t diffSum = 0;
            for (size_t nib = 0; nib < combinedSamples; ++nib)
            {
                int diff = decoderCopy.decode2bits(getNth2bit(nib, n)) - raw[i*combinedSamples + nib - combinedSamples + 1];
                diffSum += diff * diff;
            }
 
            if (diffSum < bestResults.bestDiff)
            {
                bestResults.bestDiff = diffSum;
                bestResults.bestIndex = n;
                bestResults.bestDecoder = decoderCopy;
            }
        }

        decoder = bestResults.bestDecoder; 
        result[i-1] = bestResults.bestIndex;
        squaredSum += bestResults.bestDiff;


        // if (i % 10 == 0) printf("%d\n", i);
    }

    // printf("sum: %ld\n", squaredSum);

    std::vector<uint8_t> nibbles(result.size() * combinedSamples);
    for (size_t i = 0; i < result.size(); ++i)
    {
        for (size_t nib = 0; nib < combinedSamples; ++nib)
        {
            nibbles[i * combinedSamples + nib] = getNth2bit(nib, result[i]);
        }
    }

    std::vector<uint8_t> binaryResult(nibbles.size() / 4);

    // merge 2bit values into bytes
    for (size_t n = 0; n < nibbles.size() / 4; ++n)
    {
        binaryResult[n] = (
            (nibbles[4 * n    ] << 6) +
            (nibbles[4 * n + 1] << 4) +
            (nibbles[4 * n + 2] << 2) +
            (nibbles[4 * n + 3] << 0));
    }

    binaryResult.insert(binaryResult.begin(), raw[0]);

    return binaryResult;
}

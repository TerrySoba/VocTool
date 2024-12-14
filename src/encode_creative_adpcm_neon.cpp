#include "encode_creative_adpcm_neon.h"

#include <limits>
#include <cstddef>

#include <arm_neon.h>

namespace { // annonymous namespace

struct BestStep
{
    uint8_t accumulator;
    uint8_t previous;
    size_t squaredDiff;
    uint64_t history;
};

constexpr int64_t square(int64_t a) 
{
    return a * a;
}

void calculateAllNibbles(uint8x16_t& previous, uint8x16_t& accumulators)
{
    uint8x16_t nibbles = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8x16_t data = vandq_u8(nibbles, vdupq_n_u8(7));

    uint8x16_t delta = vaddq_u8(vmulq_u8(data, accumulators), vshrq_n_u8(accumulators, 1));

    previous = vbslq_u8(vceqq_u8(vandq_u8(nibbles, vdupq_n_u8(8)), vdupq_n_u8(0)),
                        vqaddq_u8(previous, delta),
                        vqsubq_u8(previous, delta));

    uint8x16_t mask = vandq_u8(vceqq_u8(data, vdupq_n_u8(0)), vcgtq_u8(accumulators, vdupq_n_u8(1)));
    accumulators = vbslq_u8(mask, vshrq_n_u8(accumulators, 1), accumulators);

    mask = vandq_u8(vcgeq_u8(data, vdupq_n_u8(5)), vcltq_u8(accumulators, vdupq_n_u8(8)));
    accumulators = vbslq_u8(mask, vshlq_n_u8(accumulators, 1), accumulators);
}


void calculateStepRecursively(const uint8_t *data, uint8_t accumulator, uint8_t previous, size_t squaredDiff, uint64_t history, size_t recursionDepth, BestStep& bestStep)
{
    uint8x16_t accumulators = vdupq_n_u8(accumulator);
    uint8x16_t previousValues = vdupq_n_u8(previous);
    calculateAllNibbles(previousValues, accumulators);
    // nibbleDecodings+=16;

    uint8x16_t diff = vbslq_u8(vcgtq_u8(previousValues, vdupq_n_u8(*data)), vsubq_u8(previousValues, vdupq_n_u8(*data)), vsubq_u8(vdupq_n_u8(*data), previousValues));

    if (recursionDepth != 0)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            calculateStepRecursively(data + 1, accumulators[i], previousValues[i], squaredDiff + square(diff[i]), history | (i << (4 * recursionDepth)), recursionDepth - 1, bestStep);
        }
    }
    else
    {
        for (size_t i = 0; i < 16; ++i)
        {
            auto currentDiff = squaredDiff + square(diff[i]);
            if (currentDiff < bestStep.squaredDiff)
            {
                bestStep.squaredDiff = currentDiff;
                bestStep.accumulator = accumulators[i];
                bestStep.previous = previousValues[i];
                bestStep.history = history | (i << (4 * recursionDepth));
            }
        }
    }
}

} // annonymous namespace

std::vector<uint8_t> createAdpcm4BitFromRawNeon(const std::vector<uint8_t>& raw, [[maybe_unused]] uint64_t combinedNibbles)
{
    uint64_t squaredSum = 0u;
    std::vector<uint8_t> nibbles;
    nibbles.reserve(raw.size());

    BestStep bestStep;
    bestStep.accumulator = 1;
    bestStep.previous = raw[0];
    bestStep.squaredDiff = std::numeric_limits<size_t>::max();

    for (size_t i = 1; i < raw.size() - combinedNibbles; i += combinedNibbles)
    {
        uint8x16_t accumulators = vdupq_n_u8(bestStep.accumulator);
        uint8x16_t previous = vdupq_n_u8(bestStep.previous);
        bestStep.squaredDiff = std::numeric_limits<size_t>::max();
        calculateStepRecursively(&raw[i], accumulators[0], previous[0], 0, 0, combinedNibbles - 1, bestStep);

        for (int n = combinedNibbles - 1; n >= 0; --n)
        {
            nibbles.push_back((bestStep.history >> (4 * n)) & 0xf);
        }

        squaredSum += bestStep.squaredDiff;
    }

    std::vector<uint8_t> binaryResult(nibbles.size() / 2);

    // merge nibbles into bytes
    for (size_t n = 0; n < nibbles.size() / 2; ++n)
    {
        binaryResult[n] = ((nibbles[2 * n] << 4) + (nibbles[2 * n + 1]));
    }

    binaryResult.insert(binaryResult.begin(), raw[0]);

    return binaryResult;
}

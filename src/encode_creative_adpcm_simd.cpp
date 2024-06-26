#include "encode_creative_adpcm_simd.h"

#include "vectorclass.h"

#include <limits>

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

void calculateAllNibbles(Vec16uc& previous, Vec16uc& accumulators)
{
    Vec16uc nibbles(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    Vec16uc data = nibbles & 7;

    Vec16uc delta =
        (data * accumulators) +
        (accumulators / 2);

    previous = select((nibbles & 8) != 0, add_saturated(previous, delta), sub_saturated(previous, delta));

    Vec16cb mask = (data == 0) & (accumulators > 1);
    accumulators = select(mask, accumulators >> 1, accumulators);

    mask = (data >= 5) & (accumulators < 8);
    accumulators = select(mask, accumulators << 1, accumulators);
}


void calculateStepRecursively(const uint8_t *data, uint8_t accumulator, uint8_t previous, size_t squaredDiff, uint64_t history, size_t recursionDepth, BestStep& bestStep)
{
    Vec16uc accumulators(accumulator);
    Vec16uc previousValues(previous);
    calculateAllNibbles(previousValues, accumulators);
    // nibbleDecodings+=16;

    Vec16uc diff = select(previousValues > *data, previousValues - (*data), (*data) - previousValues);

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

std::vector<uint8_t> createAdpcm4BitFromRawSIMD(const std::vector<uint8_t>& raw, [[maybe_unused]] uint64_t combinedNibbles)
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
        Vec16uc accumulators(bestStep.accumulator);
        Vec16uc previous(bestStep.previous);
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

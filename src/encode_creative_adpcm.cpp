#include "encode_creative_adpcm.h"
#include "decode_creative_adpcm.h"

#include "omp.h"

#include <limits>
#include <cassert>
#include <memory>
#include <iostream>
#include <random>
#include <algorithm>

uint64_t getNthNibble(int n, uint64_t value)
{
    return (0xf & (value >> (4 * n)));
}

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


struct Best
{
    uint64_t bestIndex = 0;
    uint64_t bestDiff = std::numeric_limits<uint64_t>::max();
    CreativeAdpcmDecoder4Bit bestDecoder = CreativeAdpcmDecoder4Bit(0);
};

/**
 * Encodes the given sequence of unsigned 8bit values to 4bit ADPCM.
 * The first 8bit value is stored "as is", but the following values are
 * compressed to 4bit values. This almost halves the size.
 * 
 * The encoder uses an ADPCM decoder and tries every possible input
 * until it gets the output that most closely matches the input value.
 * The parameter combinedNibbles controlls the number of nibbles
 * that are combined. Each additional nibble muliplies the runtime
 * by 16. A value between 3 and 5 produces good results.
 * Increasing the number further does not add much quality improvements,
 * but drastically increases the runtime, so 5 is the default.
 */
std::vector<uint8_t> createAdpcm4BitFromRawOpenMP(const std::vector<uint8_t>& raw, uint64_t combinedNibbles)
{
    uint64_t squaredSum = 0u;

    CreativeAdpcmDecoder4Bit decoder(raw[0]);
    
    std::vector<uint64_t> result(raw.size() / combinedNibbles);

    for (size_t i = 1; i < raw.size() / combinedNibbles; ++i)
    {
        std::vector<Best> bestResults(omp_get_max_threads());

        // try every possible input for the decoder
        #pragma omp parallel for
        for (int64_t n = 0; n < static_cast<int64_t>(constPow(16, combinedNibbles)); ++n)
        {
            CreativeAdpcmDecoder4Bit decoderCopy = decoder;
            uint64_t diffSum = 0;
            for (size_t nib = 0; nib < combinedNibbles; ++nib)
            {
                int32_t diff = (int32_t)decoderCopy.decodeNibble(getNthNibble(nib, n)) - (int32_t)raw[i*combinedNibbles + nib - combinedNibbles + 1];
                diffSum += diff * diff;
            }
 
            auto& best = bestResults.at(omp_get_thread_num());

            if (diffSum < best.bestDiff)
            {
                best.bestDiff = diffSum;
                best.bestIndex = n;
                best.bestDecoder = decoderCopy;
            }
        }

        // now merge results from threads
        uint64_t bestIndex = 0;
        uint64_t bestDiff = std::numeric_limits<uint64_t>::max();
        CreativeAdpcmDecoder4Bit bestDecoder(0);
        for (auto& best : bestResults)
        {
            // printf("diff: %d\n", best.bestDiff);
            if (best.bestDiff < bestDiff)
            {
                bestDiff = best.bestDiff;
                bestIndex = best.bestIndex;
                bestDecoder = best.bestDecoder;
            }
        }


        decoder = bestDecoder; 
        result[i-1] = bestIndex;
        squaredSum += bestDiff;


        // if (i % 10 == 0) printf("%d\n", i);
    }

    // printf("sum: %ld\n", squaredSum);

    std::vector<uint8_t> nibbles(result.size() * combinedNibbles);
    for (size_t i = 0; i < result.size(); ++i)
    {
        for (size_t nib = 0; nib < combinedNibbles; ++nib)
        {
            nibbles[i * combinedNibbles + nib] = getNthNibble(nib, result[i]);
        }
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


/**
 * Encodes the given sequence of unsigned 8bit values to 4bit ADPCM.
 * The first 8bit value is stored "as is", but the following values are
 * compressed to 4bit values. This almost halves the size.
 * 
 * The encoder uses an ADPCM decoder and tries every possible input
 * until it gets the output that most closely matches the input value.
 * The parameter combinedNibbles controlls the number of nibbles
 * that are combined. Each additional nibble muliplies the runtime
 * by 16. A value between 3 and 4 produces good results.
 * Increasing the number further does not add much quality improvements,
 * but drastically increases the runtime, so 4 is the default.
 */
std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw, uint64_t combinedNibbles)
{
    uint64_t squaredSum = 0u;

    CreativeAdpcmDecoder4Bit decoder(raw[0]);
    
    std::vector<uint64_t> result(raw.size() / combinedNibbles);

    for (size_t i = 1; i < raw.size() / combinedNibbles; ++i)
    {
        Best bestResults;

        // try every possible input for the decoder
        for (uint64_t n = 0; n < constPow(16, combinedNibbles); ++n)
        {
            CreativeAdpcmDecoder4Bit decoderCopy = decoder;
            uint64_t diffSum = 0;
            for (size_t nib = 0; nib < combinedNibbles; ++nib)
            {
                int diff = decoderCopy.decodeNibble(getNthNibble(nib, n)) - raw[i*combinedNibbles + nib - combinedNibbles + 1];
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

    std::vector<uint8_t> nibbles(result.size() * combinedNibbles);
    for (size_t i = 0; i < result.size(); ++i)
    {
        for (size_t nib = 0; nib < combinedNibbles; ++nib)
        {
            nibbles[i * combinedNibbles + nib] = getNthNibble(nib, result[i]);
        }
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


struct TrellisBranch
{
    TrellisBranch(uint8_t firstValue) :
        decoder(firstValue)
    {
    }
    CreativeAdpcmDecoder4Bit decoder;
    std::vector<uint8_t> history;
    uint64_t squaredDiff = 0;
};


std::vector<uint8_t> createAdpcm4BitFromRawTrellis(const std::vector<uint8_t>& raw, uint32_t maxBranches)
{
    assert(!raw.empty());

    int randomBranches = maxBranches / 2; // 50% random branches
    
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());

    // Initialize trellis branches, we need maxBranches * 16 + maxBranches branches to cover all possibilities
    std::vector<std::shared_ptr<TrellisBranch>> trellisBranches;
    for (int i = 0; i < maxBranches * 16 + maxBranches; ++i)
    {
        auto branch = std::make_shared<TrellisBranch>(raw.front());
        branch->history.reserve(raw.size());
        trellisBranches.push_back(branch);
    }

    for (int pos = 1; pos < raw.size(); ++pos)
    {
        // first sort branches by squaredDiff
        std::sort(trellisBranches.begin(), trellisBranches.begin() + maxBranches * 16,
            [](const std::shared_ptr<TrellisBranch>& a, const std::shared_ptr<TrellisBranch>& b)
            {
                return a->squaredDiff < b->squaredDiff;
            });
        
        // Select random branches from the sorted list (excluding the best ones we'll keep anyway)
        // We sample from branches beyond the top (maxBranches - randomBranches) to add diversity
        int numBestBranches = maxBranches - randomBranches;
        if (maxBranches * 16 > numBestBranches + randomBranches)
        {
            // Create a distribution for selecting random branches from the remaining pool
            std::uniform_int_distribution<> distrib(numBestBranches, maxBranches * 16 - 1);
            
            // Collect random indices
            std::vector<int> randomIndices;
            for (int i = 0; i < randomBranches; ++i)
            {
                randomIndices.push_back(distrib(gen));
            }
            
            // Sort random indices to avoid duplicates and maintain order
            std::sort(randomIndices.begin(), randomIndices.end());
            randomIndices.erase(std::unique(randomIndices.begin(), randomIndices.end()), randomIndices.end());
            
            // Move random branches to fill the slots after the best branches
            int targetSlot = numBestBranches;
            for (int srcIdx : randomIndices)
            {
                if (targetSlot < maxBranches)
                {
                    std::swap(trellisBranches[targetSlot], trellisBranches[srcIdx]);
                    ++targetSlot;
                }
            }
        }
        
        // std::cout << "Best diff: " << trellisBranches.front()->squaredDiff << "\n";

        #pragma omp parallel for
        for (int branchNo = 0; branchNo < maxBranches; ++branchNo)
        {
            for (uint8_t nibble = 0; nibble < 16; ++nibble)
            {
                auto& currentBranch = trellisBranches[branchNo];
                auto decoderCopy = currentBranch->decoder;
                uint8_t decodedValue = decoderCopy.decodeNibble(nibble);
                int32_t diff = (int32_t)decodedValue - (int32_t)raw[pos];
                uint64_t newSquaredDiff = currentBranch->squaredDiff + diff * diff;
                auto& newBranch = trellisBranches[ maxBranches + branchNo * 16 + nibble ];
                newBranch->decoder = decoderCopy;
                newBranch->squaredDiff = newSquaredDiff;
                newBranch->history = currentBranch->history;
                newBranch->history.push_back(nibble);
            }
        }

        // move the first maxBranches branches to the back, so they will be ignored in next sorting
        for (int branchNo = 0; branchNo < maxBranches; ++branchNo)
        {
            trellisBranches.push_back(trellisBranches.front());
            trellisBranches.erase(trellisBranches.begin());
        }
    }

    // find best branch
    std::shared_ptr<TrellisBranch> bestBranch = trellisBranches.front();
    for (int i = 0; i < maxBranches * 16; ++i)
    {
        if (trellisBranches[i]->squaredDiff < bestBranch->squaredDiff)
        {
            bestBranch = trellisBranches[i];
        }
    }


    std::vector<uint8_t> nibbles = bestBranch->history;
    std::vector<uint8_t> binaryResult(nibbles.size() / 2);

    // merge nibbles into bytes
    for (size_t n = 0; n < nibbles.size() / 2; ++n)
    {
        binaryResult[n] = ((nibbles[2 * n] << 4) + (nibbles[2 * n + 1]));
    }

    binaryResult.insert(binaryResult.begin(), raw[0]);

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

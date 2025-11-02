#ifndef COMPARE_AUDIO_H
#define COMPARE_AUDIO_H

#include <span>
#include <cstdint>

struct ComparisonResult
{
    double averageDifference;
    double maxDifference;
};

ComparisonResult computeAudioDifference(const std::span<uint8_t>& audio1, const std::span<uint8_t>& audio2, uint32_t sampleRateHz);


#endif // COMPARE_AUDIO_H
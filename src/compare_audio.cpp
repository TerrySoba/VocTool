#include "compare_audio.h"

#include <algorithm>

template <typename T>
constexpr int64_t square(T value)
{
    return (int64_t)value * (int64_t)value;
}

ComparisonResult computeAudioDifference(const std::span<uint8_t>& audio1, const std::span<uint8_t>& audio2, uint32_t sampleRateHz)
{
    std::size_t minSize = std::min(audio1.size(), audio2.size());

    uint64_t totalDifference = 0;
    uint64_t maxDifference = 0;
    for (size_t i = 0; i < minSize; ++i)
    {
        uint64_t diff = std::abs(static_cast<int>(audio1[i]) - static_cast<int>(audio2[i]));
        totalDifference += square(diff);
        maxDifference = std::max(maxDifference, diff);
    }

    double averageDifference = static_cast<double>(totalDifference) / minSize;
    return { averageDifference, static_cast<double>(maxDifference) };
}

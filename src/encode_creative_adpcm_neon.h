#ifndef ENCODE_CREATIVE_ADPCM_NEON_H
#define ENCODE_CREATIVE_ADPCM_NEON_H

#include <vector>
#include <cstdint>

std::vector<uint8_t> createAdpcm4BitFromRawNeon(const std::vector<uint8_t>& raw, [[maybe_unused]] uint64_t combinedNibbles = 5);

#endif

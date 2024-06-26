#ifndef ENCODE_CREATIVE_ADPCM_SIMD_H
#define ENCODE_CREATIVE_ADPCM_SIMD_H

#include "encode_creative_adpcm_simd.h"

#include <vector>
#include <cstdint>

std::vector<uint8_t> createAdpcm4BitFromRawSIMD(const std::vector<uint8_t>& raw, [[maybe_unused]] uint64_t combinedNibbles = 5);

#endif

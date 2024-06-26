#ifndef ENCODE_CREATIVE_ADPCM_H

#include <vector>
#include <cstdint>

std::vector<uint8_t> createAdpcm4BitFromRawOpenMP(const std::vector<uint8_t>& raw, uint64_t combinedNibbles = 5);
std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw, uint64_t combinedNibbles = 4);
std::vector<uint8_t> createAdpcm2BitFromRaw(const std::vector<uint8_t>& raw, uint64_t combinedSamples = 4);

#endif

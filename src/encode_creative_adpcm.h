#ifndef ENCODE_CREATIVE_ADPCM_H
#define ENCODE_CREATIVE_ADPCM_H

#include <vector>
#include <cstdint>

/**
 * Encodes unsigned 8-bit samples to Creative 4-bit ADPCM.
 *
 * The first sample is stored as reference byte, every following sample as one nibble.
 * Uses the Viterbi algorithm over the complete decoder state, which finds the encoding
 * with the minimum squared error between input and decoded output.
 */
std::vector<uint8_t> createAdpcm4BitFromRaw(const std::vector<uint8_t>& raw);

/**
 * Encodes unsigned 8-bit samples to Creative 2-bit ADPCM.
 *
 * The first sample is stored as reference byte, every following sample as 2 bits.
 * Uses the Viterbi algorithm over the complete decoder state, which finds the encoding
 * with the minimum squared error between input and decoded output.
 */
std::vector<uint8_t> createAdpcm2BitFromRaw(const std::vector<uint8_t>& raw);

#endif

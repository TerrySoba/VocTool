#ifndef ENCODE_CREATIVE_ADPCM_H
#define ENCODE_CREATIVE_ADPCM_H

#include "noise_shaping.h"

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

/**
 * Encodes unsigned 8-bit samples to Creative 2.6-bit ADPCM (VOC codec 2, "3-bit" ADPCM).
 *
 * The first sample is stored as reference byte, then three samples per byte (3 + 3 + 2 bits).
 * Uses the Viterbi algorithm over the complete decoder state, which finds the encoding
 * with the minimum squared error between input and decoded output.
 */
std::vector<uint8_t> createAdpcm3BitFromRaw(const std::vector<uint8_t>& raw);

/**
 * Encodes samples in range -1.0 .. 1.0 to Creative 4-bit ADPCM, with optional noise shaping.
 *
 * The samples are used with full precision (not rounded to 8 bit first), so the error
 * of the 8-bit output is minimized (and shaped) with respect to the original signal.
 */
std::vector<uint8_t> createAdpcm4Bit(const std::vector<double>& samples, const NoiseShaping& shaping = {});

/**
 * Encodes samples in range -1.0 .. 1.0 to Creative 2-bit ADPCM, with optional noise shaping.
 * See createAdpcm4Bit().
 */
std::vector<uint8_t> createAdpcm2Bit(const std::vector<double>& samples, const NoiseShaping& shaping = {});

/**
 * Encodes samples in range -1.0 .. 1.0 to Creative 2.6-bit ADPCM, with optional noise shaping.
 * See createAdpcm4Bit().
 */
std::vector<uint8_t> createAdpcm3Bit(const std::vector<double>& samples, const NoiseShaping& shaping = {});

/**
 * Converts samples in range -1.0 .. 1.0 to unsigned 8-bit PCM, with optional noise shaping of the
 * rounding error. Without noise shaping this is the same as rounding to the nearest value.
 * Uses the same Viterbi search as the ADPCM encoders (one output byte per sample).
 */
std::vector<uint8_t> createPcm8Bit(const std::vector<double>& samples, const NoiseShaping& shaping = {});

#endif

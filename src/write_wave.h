#ifndef WRITE_WAVE_H
#define WRITE_WAVE_H

#include "read_wave.h"

void write8bitMonoWaveFile(const std::string& filename, uint32_t sampleRate, const std::vector<uint8_t>& data);


#endif
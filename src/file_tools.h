#ifndef COMMAND_FILE_TOOLS_H
#define COMMAND_FILE_TOOLS_H

#include <vector>
#include <string>
#include <cstdint>

std::vector<uint8_t> loadFile(const std::string& filename);
void storeFile(const std::string& filename, const std::vector<uint8_t>& data);

#endif


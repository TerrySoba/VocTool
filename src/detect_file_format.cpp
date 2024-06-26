#include "detect_file_format.h"

#include <fstream>
#include <stdexcept>

FileFormat detectFileFormat(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("File not found");
    }

    char buffer[4];
    file.read(buffer, 4);

    if (file.gcount() < 4)
    {
        return FileFormat::UNKNOWN;
    }

    if (buffer[0] == 'R' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == 'F')
    {
        return FileFormat::WAV;
    }
    else if (buffer[0] == 'C' && buffer[1] == 'r' && buffer[2] == 'e' && buffer[3] == 'a')
    {
        return FileFormat::VOC;
    }

    return FileFormat::UNKNOWN;
}
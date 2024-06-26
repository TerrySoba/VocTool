#ifndef _DETECT_FILE_FORMAT_H
#define _DETECT_FILE_FORMAT_H

#include <string>

enum class FileFormat
{
    UNKNOWN = 0,
    WAV = 1,
    VOC = 2
};

FileFormat detectFileFormat(const std::string& filePath);


#endif
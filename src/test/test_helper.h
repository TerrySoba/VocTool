#ifndef TEST_HELPER_H
#define TEST_HELPER_H

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

namespace { // annonymous namespace

std::string getTestDataDir()
{
    return TEST_DATA_DIR;
}


template <typename SampleType>
void dumpCsv(const std::vector<SampleType>& input, const std::string& filename)
{
    std::ofstream file(filename);
    for (size_t i = 0; i < input.size(); ++i)
    {
        file << (double) input[i] << "\n";
    }
}

template <typename SampleType>
void dumpRaw(const std::vector<SampleType>& input, const std::string& filename)
{
    std::ofstream file(filename, std::ios::binary);
    for (size_t i = 0; i < input.size(); ++i)
    {
        file.write((char*)&input[i], sizeof(SampleType));
    }
}

std::vector<uint8_t> readRaw(const std::string& filename)
{
    FILE* file = fopen(filename.c_str(), "rb");
    if (!file)
    {
        throw std::runtime_error("Could not open file " + filename);
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    std::vector<uint8_t> buffer(size);
    auto count = fread(buffer.data(), 1, size, file);
    fclose(file);

    if (count != size)
    {
        throw std::runtime_error("Could not read file " + filename + " completely.");
    }

    return buffer;
}


template <typename SampleType>
bool vectorsAreEqual(const std::vector<SampleType>& a, const std::vector<SampleType>& b)
{
    if (a.size() != b.size())
    {
        std::cout << "Size mismatch: " << a.size() << " != " << b.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i] != b[i])
        {
            std::cout << "Mismatch at index " << i << ": " << a[i] << " != " << b[i] << std::endl;
            return false;
        }
    }
    return true;
}

} // annonymous namespace

#endif
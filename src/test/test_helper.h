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
    std::ifstream t(filename);
    std::stringstream buffer;
    buffer << t.rdbuf();
    std::string str = buffer.str();
    return std::vector<uint8_t>(str.begin(), str.end());
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
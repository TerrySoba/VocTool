#include "file_tools.h"
#include "command_line_parser.h"
#include "voc_format.h"
#include "decode_creative_adpcm.h"
#include "write_wave.h"


#include <iostream>
#include <map>

int main(int argc, char* argv[])
{
    try
    {
        clp::CommandLineParser parser(
            "Program to convert VOC files into WAVE files including ADPCM decompression.");
        parser.addParameter("input", "i", "Name of the input file", clp::ParameterRequired::yes);
        parser.addParameter("output", "o", "Name of the output file", clp::ParameterRequired::yes);
        parser.parse(argc, argv);

        
        auto inputFilename = parser.getValue<std::string>("input");
        auto outputFilename = parser.getValue<std::string>("output");

        auto vocData = readVocFile(inputFilename);

        std::cout << "Read VOC file " << inputFilename << std::endl;
        std::cout << "  Sample format: " << vocData.sampleFormat << std::endl;
        std::cout << "  Sample size: " << vocData.sampleData.size() << std::endl;
        std::cout << "  Time constant: " << (int)vocData.timeConstant << std::endl;
        std::cout << "  Major version: " << (int)vocData.majorVersion << std::endl;
        std::cout << "  Minor version: " << (int)vocData.minorVersion << std::endl;

        auto pcmVoc = decodeToPcm(vocData);
        
        std::cout << "Decoded VOC size " << pcmVoc.sampleData.size() << std::endl;
       
        write8bitMonoWaveFile(outputFilename, timeConstantToFrequency(vocData.timeConstant), pcmVoc.sampleData);

        return 0;
    }
    catch (const std::exception &e)
    {
        printf("Error: %s\n", e.what());
        return 1;
    }
}

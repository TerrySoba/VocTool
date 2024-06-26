#include <catch2/catch_test_macros.hpp>

#include "test_helper.h"

#include "detect_file_format.h"

TEST_CASE("Detect file format formats")
{
    REQUIRE(detectFileFormat(getTestDataDir() + "/jetpack.voc") == FileFormat::VOC);
    REQUIRE(detectFileFormat(getTestDataDir() + "/jetpack.wav") == FileFormat::WAV);
    REQUIRE(detectFileFormat(getTestDataDir() + "/16bit_mono_48000.raw") == FileFormat::UNKNOWN);
}

TEST_CASE("Detect file format missing file")
{
    // check if function throws exception when file does not exist
    REQUIRE_THROWS_AS(detectFileFormat("non_existing_file"), std::runtime_error);
}

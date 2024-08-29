#include "catch_importer.h"
#include "test_helper.h"
#include "voc_format.h"

#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>

TEST_CASE("Test Voc Decoding Wrong Filename")
{
    REQUIRE_THROWS_AS(readVocFile(""), std::runtime_error);
    REQUIRE_THROWS_AS(readVocFile("non_existing_file"), std::runtime_error);
}

TEST_CASE("Test Voc Decoding ADPCM2")
{
    auto vocfile = readVocFile(getTestDataDir() + "/jetpack.voc");
    REQUIRE(vocfile.sampleFormat == VOC_FORMAT_ADPCM_2BIT);
    REQUIRE((int)vocfile.timeConstant == 131);
    REQUIRE(vocfile.majorVersion == 1);
    REQUIRE(vocfile.minorVersion == 10);

    auto pcm = decodeToPcm(vocfile);
    REQUIRE(pcm.sampleFormat == VOC_FORMAT_PCM_8BIT);
    REQUIRE(pcm.timeConstant == vocfile.timeConstant);
    REQUIRE(pcm.majorVersion == vocfile.majorVersion);
    REQUIRE(pcm.minorVersion == vocfile.minorVersion);

    // compare against reference
    auto reference = readRaw(getTestDataDir() + "/jetpack_decoded.raw");
    REQUIRE(pcm.sampleData.size() == reference.size());
    REQUIRE(pcm.sampleData == reference);
}

TEST_CASE("Test Voc Decoding ADPCM4")
{
    auto vocfile = readVocFile(getTestDataDir() + "/jetpack_adpcm4.voc");
    REQUIRE(vocfile.sampleFormat == VOC_FORMAT_ADPCM_4BIT);
    REQUIRE((int)vocfile.timeConstant == 131);
    REQUIRE(vocfile.majorVersion == 1);
    REQUIRE(vocfile.minorVersion == 10);

    auto pcm = decodeToPcm(vocfile);
    REQUIRE(pcm.sampleFormat == VOC_FORMAT_PCM_8BIT);
    REQUIRE(pcm.timeConstant == vocfile.timeConstant);
    REQUIRE(pcm.majorVersion == vocfile.majorVersion);
    REQUIRE(pcm.minorVersion == vocfile.minorVersion);

    // compare against reference
    auto reference = readRaw(getTestDataDir() + "/jetpack_adpcm4_decoded.raw");
    REQUIRE(pcm.sampleData.size() == reference.size());
    REQUIRE(pcm.sampleData == reference);
}

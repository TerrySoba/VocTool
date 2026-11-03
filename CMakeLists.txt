cmake_minimum_required(VERSION 3.20)
project(voctool)
set(CMAKE_CXX_STANDARD 20)

find_package(Catch2 REQUIRED)
if(Catch2_VERSION VERSION_GREATER_EQUAL 3.0)
    set(CATCH2_INCLUDE_FILE "catch2/catch_test_macros.hpp")
else()
    set(CATCH2_INCLUDE_FILE "catch2/catch.hpp")
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/test/catch_importer.h.in ${CMAKE_BINARY_DIR}/catch_importer.h)

# libsamplerate: prefer its CMake package config, fall back to pkg-config
find_package(SampleRate CONFIG QUIET)
if(NOT TARGET SampleRate::samplerate)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
    add_library(SampleRate::samplerate ALIAS PkgConfig::SAMPLERATE)
endif()

add_library(${PROJECT_NAME}_lib STATIC
    src/file_tools.cpp
    src/voc_format.cpp
    src/read_wave.cpp
    src/write_wave.cpp
    src/resampling.cpp
    src/encode_creative_adpcm.cpp
    src/detect_file_format.cpp
    src/compare_audio.cpp
)

target_link_libraries(${PROJECT_NAME}_lib
    PRIVATE SampleRate::samplerate)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    ${PROJECT_NAME}_lib
)

add_executable(${PROJECT_NAME}_test
    src/test/resampling_test.cpp
    src/test/detect_file_format_test.cpp
    src/test/voc_format_test.cpp
    src/test/encode_creative_adpcm_test.cpp
    )

target_include_directories(${PROJECT_NAME}_test PRIVATE 
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(${PROJECT_NAME}_test
    ${PROJECT_NAME}_lib
    Catch2::Catch2WithMain)

target_include_directories(${PROJECT_NAME}_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME}_test PUBLIC TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/testdata\")
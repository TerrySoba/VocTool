cmake_minimum_required(VERSION 3.20)
project(voctool)
set(CMAKE_CXX_STANDARD 20)

set(USE_X86_SIMD false)
set(USE_ARM_SIMD false)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i[3-6]86")
    message(STATUS "Building for x86/x86_64")
    set(USE_X86_SIMD true)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    message(STATUS "Building for ARM/ARM64")
    set(USE_ARM_SIMD true)
else()
    message(FATAL_ERROR "Unsupported architecture: " ${CMAKE_SYSTEM_PROCESSOR})
endif()

find_package(Catch2 REQUIRED)
if(Catch2_VERSION VERSION_GREATER_EQUAL 3.0)
    set(CATCH2_INCLUDE_FILE "catch2/catch_test_macros.hpp")
else()
    set(CATCH2_INCLUDE_FILE "catch2/catch.hpp")
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/test/catch_importer.h.in ${CMAKE_BINARY_DIR}/catch_importer.h)

find_package(OpenMP REQUIRED)

if (USE_X86_SIMD)
    add_library(vectorclass INTERFACE)

    target_include_directories(vectorclass INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vectorclass
    )
endif()

add_library(${PROJECT_NAME}_lib STATIC
    src/file_tools.cpp
    src/voc_format.cpp
    src/read_wave.cpp
    src/write_wave.cpp
    src/resampling.cpp
    src/encode_creative_adpcm.cpp
    src/detect_file_format.cpp
)

if (USE_ARM_SIMD)
    target_sources(${PROJECT_NAME}_lib PRIVATE
        src/encode_creative_adpcm_neon.cpp
    )
endif()

if (USE_X86_SIMD)
    target_sources(${PROJECT_NAME}_lib PRIVATE
        src/encode_creative_adpcm_simd.cpp
    )
    target_link_libraries(${PROJECT_NAME}_lib
        PUBLIC vectorclass
    )
endif()

target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC OpenMP::OpenMP_CXX)

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
    )

target_include_directories(${PROJECT_NAME}_test PRIVATE 
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(${PROJECT_NAME}_test
    ${PROJECT_NAME}_lib
    Catch2::Catch2WithMain)

target_include_directories(${PROJECT_NAME}_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME}_test PUBLIC TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/testdata\")
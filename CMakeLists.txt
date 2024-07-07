cmake_minimum_required(VERSION 3.9)
project(voctool)
set(CMAKE_CXX_STANDARD 20)

if(UNIX)
    find_package(Catch2 REQUIRED)
endif(UNIX)

#find_package(vectorclass REQUIRED)
find_package(OpenMP REQUIRED)

add_library(vectorclass INTERFACE)
target_include_directories(vectorclass INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vectorclass
)

add_library(${PROJECT_NAME}_lib STATIC
    src/file_tools.cpp
    src/voc_format.cpp
    src/read_wave.cpp
    src/write_wave.cpp
    src/resampling.cpp
    src/encode_creative_adpcm.cpp
    src/encode_creative_adpcm_simd.cpp
    src/detect_file_format.cpp
)

target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC vectorclass
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

target_link_libraries(${PROJECT_NAME}_test
    ${PROJECT_NAME}_lib
    Catch2::Catch2WithMain)

target_include_directories(${PROJECT_NAME}_test PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME}_test PUBLIC TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/testdata\")
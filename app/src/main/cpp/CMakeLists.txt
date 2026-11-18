cmake_minimum_required(VERSION 3.22)
project(vstghost LANGUAGES C CXX)

include(FetchContent)

# zstd (BSD-3-Clause / GPL-2.0 dual). Only the decompressor is compiled.
# SOURCE_SUBDIR points at a non-existent dir so FetchContent only downloads
# and does not run zstd's own CMake project.
FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR do-not-configure
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(zstd)

file(GLOB ZSTD_SOURCES
    ${zstd_SOURCE_DIR}/lib/common/*.c
    ${zstd_SOURCE_DIR}/lib/decompress/*.c)
add_library(zstd_decompress STATIC ${ZSTD_SOURCES})
target_include_directories(zstd_decompress PUBLIC ${zstd_SOURCE_DIR}/lib)
# No asm Huffman decoder (it is x86_64-only anyway), no legacy formats.
target_compile_definitions(zstd_decompress PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0)

find_package(oboe REQUIRED CONFIG)
find_package(androidaudioplugin REQUIRED CONFIG)

set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../shared)

# Phase 5: the user's JUCE plugins as native libraries (lib vg_*.so).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../native native)
add_library(vstghost SHARED
    jni.cpp
    extractor.cpp
    engine.cpp
    aap_plugin.cpp
    bench.c
    wl_jni.cpp
    native_host.cpp
    ${SHARED_DIR}/wl/vg_wl.cpp
    ${SHARED_DIR}/ipc/bridge_client.cpp)

target_include_directories(vstghost PRIVATE ${SHARED_DIR} ${SHARED_DIR}/ipc ${CMAKE_CURRENT_SOURCE_DIR}/../../../../native)
target_link_libraries(vstghost PRIVATE zstd_decompress oboe::oboe androidaudioplugin::androidaudioplugin android log)
target_compile_options(vstghost PRIVATE -Wall -Wextra)
# Benchmark must not use FMA contraction, so its checksum matches the x86_64 build.
set_source_files_properties(bench.c PROPERTIES COMPILE_OPTIONS "-O2;-ffp-contract=off")

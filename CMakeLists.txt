cmake_minimum_required(VERSION 3.20)

project(mpxcast VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MPXCAST_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0 AND NOT GIT_DESCRIBE STREQUAL "v${PROJECT_VERSION}")
        string(APPEND MPXCAST_VERSION "+${GIT_DESCRIBE}")
    endif()
endif()

string(REPLACE "\\" "\\\\" MPXCAST_VERSION_C_STRING "${MPXCAST_VERSION}")
string(REPLACE "\"" "\\\"" MPXCAST_VERSION_C_STRING "${MPXCAST_VERSION_C_STRING}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/core")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h"
    @ONLY
)

find_path(MICROHTTPD_INCLUDE_DIR microhttpd.h REQUIRED)
find_library(MICROHTTPD_LIBRARY microhttpd REQUIRED)
find_path(LIQUID_INCLUDE_DIR liquid/liquid.h REQUIRED)
find_library(LIQUID_LIBRARY liquid REQUIRED)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h REQUIRED)
find_library(RTLSDR_LIBRARY rtlsdr REQUIRED)
find_package(Threads REQUIRED)

set(COVER_PNG ${CMAKE_CURRENT_SOURCE_DIR}/assets/mpxcast.png)
set(COVER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/generated/cover.c)

add_custom_command(
    OUTPUT ${COVER_SOURCE}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${COVER_PNG}
        -DOUTPUT_FILE=${COVER_SOURCE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_cover.cmake
    DEPENDS ${COVER_PNG} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_cover.cmake
    VERBATIM
)

add_executable(mpxcast
    src/audio/pcm.c
    src/audio/wav.c
    src/core/app.c
    src/core/logging.c
    src/dsp/deemphasis.c
    src/dsp/fir.c
    src/dsp/fm_discriminator.c
    src/dsp/fm_pipeline.c
    src/dsp/fm_mono.c
    src/dsp/fm_stereo.c
    src/dsp/liquid_resampler.c
    src/dsp/mpx_filter_bank.c
    src/dsp/rds_demod.c
    src/input/rtl/rtl_iq_decimator.c
    src/input/rtl/rtl_source.c
    src/main.c
    src/rds/block_sync.c
    src/rds/model.c
    src/rds/parser.c
    src/rds/printer.c
    src/stream/http.c
    src/stream/icy.c
    src/stream/mux_mpegts.c
    src/stream/mux_wav.c
    src/stream/server.c
    src/stream/session.c
    ${COVER_SOURCE}
)

target_include_directories(mpxcast PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${MICROHTTPD_INCLUDE_DIR}
)

target_include_directories(mpxcast SYSTEM PRIVATE
    ${LIQUID_INCLUDE_DIR}
    ${RTLSDR_INCLUDE_DIR}
)

target_compile_definitions(mpxcast PRIVATE
    _POSIX_C_SOURCE=200809L
)

target_link_libraries(mpxcast PRIVATE
    ${MICROHTTPD_LIBRARY}
    ${LIQUID_LIBRARY}
    ${RTLSDR_LIBRARY}
    Threads::Threads
    m
)

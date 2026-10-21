cmake_minimum_required(VERSION 3.10)

if(APPLE)
  # Universal binaries that run on Apple silicon and Intel Macs.
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Owl-Maintain/brlaser CXX)
set(BRLASER_VERSION "6.2.8")
set(HL2270DW_VERSION "1.0.0")

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to RelWithDebInfo")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type (default RelWithDebInfo)" FORCE)
endif()

include(CMakePushCheckState)
include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)


## Enable assertions for all builds
## (cmake by default sets NDEBUG for release builds)

foreach(var
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_MINSIZEREL)
  string(REGEX REPLACE "(^| )[/-]D *NDEBUG($| )" " " "${var}" "${${var}}")
endforeach()


## Configure the compiler

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

macro(extra_cxx_compiler_flag FLAG)
  string(REGEX REPLACE "[^A-Za-z_0-9]" "_" SFLAG ${FLAG})
  check_cxx_compiler_flag(${FLAG} COMPILER_SUPPORT_${SFLAG})
  if(COMPILER_SUPPORT_${SFLAG})
    set(EXTRA_CXX_FLAGS "${EXTRA_CXX_FLAGS} ${FLAG}")
  endif()
endmacro(extra_cxx_compiler_flag)

# Compiler warnings
extra_cxx_compiler_flag("-Wall")
extra_cxx_compiler_flag("-Wno-missing-braces")
extra_cxx_compiler_flag("-Wdate-time")
# Some hardening flags
extra_cxx_compiler_flag("-fstack-protector-strong")
extra_cxx_compiler_flag("-Wformat")
extra_cxx_compiler_flag("-Wformat -Werror=format-security")
extra_cxx_compiler_flag("-D_FORTIFY_SOURCE=2")
# Enable the supported flags, but give priority to CXXFLAGS env var
set(CMAKE_CXX_FLAGS "${EXTRA_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")


## Configure CUPS

if(APPLE)
  # Use the system CUPS, not a Homebrew copy that may be on the PATH.
  find_program(CUPS_CONFIG NAMES cups-config PATHS /usr/bin NO_DEFAULT_PATH)
endif()
find_program(CUPS_CONFIG NAMES cups-config)
if(NOT CUPS_CONFIG)
  message(FATAL_ERROR "cups-config command not found. Are the CUPS development packages installed?")
endif()

if(NOT CUPS_DATA_DIR)
  execute_process(
    COMMAND "${CUPS_CONFIG}" --datadir
    OUTPUT_VARIABLE CUPS_DATA_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT CUPS_SERVER_BIN)
  execute_process(
    COMMAND "${CUPS_CONFIG}" --serverbin
    OUTPUT_VARIABLE CUPS_SERVER_BIN
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT CUPS_CFLAGS)
  execute_process(
    COMMAND "${CUPS_CONFIG}" --cflags
    OUTPUT_VARIABLE CUPS_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT CUPS_LDFLAGS)
  execute_process(
    COMMAND "${CUPS_CONFIG}" --ldflags
    OUTPUT_VARIABLE CUPS_LDFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT CUPS_LIBS)
  execute_process(
    COMMAND "${CUPS_CONFIG}" --image --libs
    OUTPUT_VARIABLE CUPS_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

cmake_push_check_state()
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${CUPS_CFLAGS}")
check_include_file_cxx(cups/raster.h HAVE_CUPS_RASTER_H)
cmake_pop_check_state()
if(NOT HAVE_CUPS_RASTER_H)
  message(SEND_ERROR "<cups/raster.h> header not found. Are the CUPS development packages installed?")
endif()


## Build instructions

configure_file(
  "${PROJECT_SOURCE_DIR}/brlaser.drv.in"
  "${PROJECT_BINARY_DIR}/brlaser.drv")

configure_file(
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_BINARY_DIR}/config.h")

include_directories("${PROJECT_BINARY_DIR}")

add_executable(rastertobrlaser src/main.cc src/job.cc src/line.cc src/debug.cc)
target_compile_options(rastertobrlaser PRIVATE ${CUPS_CFLAGS})
target_link_libraries(rastertobrlaser ${CUPS_LIBS})
target_link_libraries(rastertobrlaser ${CUPS_LDFLAGS})

add_executable(brdecode EXCLUDE_FROM_ALL src/brdecode.cc)
add_executable(test_lest test/test_lest.cc)
add_executable(test_line test/test_line.cc src/line.cc)
add_executable(test_block test/test_block.cc)
add_executable(test_job test/test_job.cc src/job.cc src/line.cc)

enable_testing()
add_test(test_lest test_lest)
add_test(test_line test_line)
add_test(test_block test_block)
add_test(test_job test_job)



## HL-2270DW driver

if(APPLE)
  set(HL2270DW_DEFAULT_FILTER_DIR "/Library/Printers/hl2270dw/Filter")
else()
  set(HL2270DW_DEFAULT_FILTER_DIR "${CUPS_SERVER_BIN}/filter")
endif()
set(HL2270DW_FILTER_DIR "${HL2270DW_DEFAULT_FILTER_DIR}" CACHE PATH
  "Directory the PPD expects the HL-2270DW filters in")

configure_file(
  "${PROJECT_SOURCE_DIR}/src/hl2270dw/hl_config.h.in"
  "${PROJECT_BINARY_DIR}/hl_config.h")
configure_file(
  "${PROJECT_SOURCE_DIR}/hl2270dw.drv.in"
  "${PROJECT_BINARY_DIR}/hl2270dw.drv"
  @ONLY)

add_library(hl2270dw_core STATIC
  src/line.cc
  src/hl2270dw/hljob.cc
  src/hl2270dw/options.cc
  src/hl2270dw/paper.cc
  src/hl2270dw/supplies.cc)
target_compile_options(hl2270dw_core PRIVATE ${CUPS_CFLAGS})

add_executable(rastertohl2270dw src/hl2270dw/rastertohl2270dw.cc)
add_executable(commandtohl2270dw src/hl2270dw/commandtohl2270dw.cc)
foreach(target rastertohl2270dw commandtohl2270dw)
  target_compile_options(${target} PRIVATE ${CUPS_CFLAGS})
  target_link_libraries(${target} hl2270dw_core ${CUPS_LIBS} ${CUPS_LDFLAGS})
endforeach()

add_executable(test_hl2270dw test/test_hl2270dw.cc)
target_include_directories(test_hl2270dw PRIVATE "${PROJECT_SOURCE_DIR}/src/hl2270dw")
target_link_libraries(test_hl2270dw hl2270dw_core ${CUPS_LIBS} ${CUPS_LDFLAGS})
add_test(test_hl2270dw test_hl2270dw)

# The PPD is generated from hl2270dw.drv with ppdc.
find_program(PPDC NAMES ppdc PATHS /usr/bin /usr/sbin)
if(PPDC)
  add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/ppd/hl2270dw.ppd"
    COMMAND "${PPDC}" -d "${PROJECT_BINARY_DIR}/ppd" --lf
            "${PROJECT_BINARY_DIR}/hl2270dw.drv"
    DEPENDS "${PROJECT_BINARY_DIR}/hl2270dw.drv"
    COMMENT "Generating hl2270dw.ppd")
  add_custom_target(hl2270dw_ppd ALL
    DEPENDS "${PROJECT_BINARY_DIR}/ppd/hl2270dw.ppd")
else()
  message(WARNING "ppdc not found; the HL-2270DW PPD will not be built")
endif()

# Autotools-style "make check" command
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND})
add_dependencies(check test_lest test_line test_block test_job test_hl2270dw)


# Installation

install(
  TARGETS rastertobrlaser
  DESTINATION "${CUPS_SERVER_BIN}/filter")
install(
  FILES "${PROJECT_BINARY_DIR}/brlaser.drv"
  DESTINATION "${CUPS_DATA_DIR}/drv")

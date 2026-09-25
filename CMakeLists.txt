# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
cmake_minimum_required(VERSION 3.16)
file(READ
  "${CMAKE_CURRENT_SOURCE_DIR}/firmware/atf1502_programmer/version.h"
  ATF_VERSION_HEADER)
string(REGEX MATCH
  "kVersion\\[\\][^=]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
  ATF_VERSION_MATCH "${ATF_VERSION_HEADER}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Could not read semantic version from version.h")
endif()
set(ATF_PROJECT_VERSION "${CMAKE_MATCH_1}")
project(atf1502_programmer VERSION ${ATF_PROJECT_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(atf_jedec cli/src/jedec.cc)
target_include_directories(atf_jedec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(atfprog cli/src/main.cc cli/src/serial.cc)
target_link_libraries(atfprog PRIVATE atf_jedec)
option(ATF_STATIC_RUNTIME "Statically link the atfprog runtime libraries" OFF)
if(MINGW OR ATF_STATIC_RUNTIME)
  target_link_options(atfprog PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(MSVC)
  target_compile_options(atf_jedec PRIVATE /W4)
  target_compile_options(atfprog PRIVATE /W4)
else()
  target_compile_options(atf_jedec PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions)
  target_compile_options(atfprog PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions)
endif()
install(TARGETS atfprog RUNTIME DESTINATION bin)
include(CTest)
if(BUILD_TESTING)
  add_test(NAME cli_version COMMAND atfprog --version)
  set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "^atfprog v${PROJECT_VERSION}")
  add_executable(atf_tests tests/core_test.cc)
  target_link_libraries(atf_tests PRIVATE atf_jedec)
  add_test(NAME core COMMAND atf_tests)
  add_executable(firmware_tests tests/firmware_test.cc)
  target_link_libraries(firmware_tests PRIVATE atf_jedec)
  target_include_directories(firmware_tests PRIVATE tests/arduino)
  add_test(NAME firmware_protocol COMMAND firmware_tests)
  if(UNIX AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
      add_test(NAME serial_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_serial.py $<TARGET_FILE:atfprog>)
      set_tests_properties(serial_integration PROPERTIES TIMEOUT 90)
    endif()
  endif()
endif()

# Formatting is opt-in locally; CI runs this target with clang-format 18.
find_program(ATF_CLANG_FORMAT NAMES clang-format-18 clang-format)
if(ATF_CLANG_FORMAT)
  file(GLOB_RECURSE ATF_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/firmware/*.ino")
  add_custom_target(format-check
    COMMAND ${ATF_CLANG_FORMAT} --dry-run --Werror ${ATF_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking Google C++ formatting"
    VERBATIM)
endif()

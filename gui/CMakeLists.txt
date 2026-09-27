# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
#
# Qt 6 desktop application. Configure from the repository root with
# -DATF_BUILD_GUI=ON; the GUI links the same programmer library as atfprog.

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets SerialPort Network)

set(CMAKE_AUTOMOC ON)

option(ATF_BUNDLE_AVRDUDE
  "Download the pinned AVRDUDE release next to the GUI (Windows only)" ${WIN32})
set(ATF_FIRMWARE_HEX "" CACHE FILEPATH
  "Leonardo firmware (.ino.hex) to bundle for firmware installation")

# Fuse-map model without widgets, shared by the application and its tests.
add_library(atf_gui_model STATIC
  src/fuse_database.cc
  src/jedec_document.cc)
target_link_libraries(atf_gui_model PUBLIC atf_jedec Qt6::Core Qt6::Gui)
qt_add_resources(atf_gui_model "fusemap"
  PREFIX "/"
  BASE resources
  FILES
    resources/fusemap/atf1502as.json
    resources/fusemap/atf1504as.json)

add_executable(atf150x-programmer WIN32
  src/device_finder.cc
  src/fuse_map_widget.cc
  src/main.cc
  src/main_window.cc
  src/programmer_worker.cc
  src/update_checker.cc)
qt_add_resources(atf150x-programmer "icons"
  PREFIX "/"
  BASE resources
  FILES
    resources/icons/atf150x-programmer.png
    resources/icons/firmware.png
    resources/icons/firmware@2x.png
    resources/icons/program.png
    resources/icons/program@2x.png)
if(WIN32)
  target_sources(atf150x-programmer PRIVATE resources/app_icon.rc)
  set_property(SOURCE resources/app_icon.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/atf150x-programmer.ico")
endif()
target_link_libraries(atf150x-programmer PRIVATE
  atf_gui_model atf_programmer Qt6::Widgets Qt6::SerialPort Qt6::Network)

foreach(atf_target atf_gui_model atf150x-programmer)
  target_compile_definitions(${atf_target} PRIVATE
    QT_NO_CAST_FROM_ASCII)
  if(NOT MSVC)
    target_compile_options(${atf_target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

# Runtime payload next to the executable, mirroring the installed layout.
set(ATF_GUI_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if(ATF_FIRMWARE_HEX)
  if(NOT EXISTS "${ATF_FIRMWARE_HEX}")
    message(FATAL_ERROR "ATF_FIRMWARE_HEX does not exist: ${ATF_FIRMWARE_HEX}")
  endif()
  configure_file("${ATF_FIRMWARE_HEX}"
    "${ATF_GUI_OUTPUT_DIR}/firmware/atf150x-leonardo-firmware.hex" COPYONLY)
else()
  message(STATUS "ATF_FIRMWARE_HEX not set; firmware installation disabled")
endif()
if(ATF_BUNDLE_AVRDUDE)
  include(cmake/Avrdude.cmake)
  atf_bundle_avrdude("${ATF_GUI_OUTPUT_DIR}/tools/avrdude")
endif()

if(BUILD_TESTING)
  add_executable(gui_model_tests tests/gui_model_test.cc)
  target_link_libraries(gui_model_tests PRIVATE atf_gui_model)
  target_compile_definitions(gui_model_tests PRIVATE QT_NO_CAST_FROM_ASCII)
  add_test(NAME gui_model COMMAND gui_model_tests)
  set_tests_properties(gui_model PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

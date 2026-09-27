# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
#
# Downloads the pinned AVRDUDE Windows release that installs the Leonardo
# firmware. AVRDUDE is GPL-2.0-or-later; its license and the matching source
# archive are bundled alongside the executable.

set(ATF_AVRDUDE_VERSION 8.2)
set(ATF_AVRDUDE_BASE "https://github.com/avrdudes/avrdude")
set(ATF_AVRDUDE_BINARY_URL
  "${ATF_AVRDUDE_BASE}/releases/download/v${ATF_AVRDUDE_VERSION}/avrdude-v${ATF_AVRDUDE_VERSION}-windows-x64.zip")
set(ATF_AVRDUDE_BINARY_SHA256
  e2a89a921e1b2fe675a319248ea968c7e28c7edc44c5fd836e2879a3e094c9cf)
set(ATF_AVRDUDE_SOURCE_URL
  "${ATF_AVRDUDE_BASE}/archive/refs/tags/v${ATF_AVRDUDE_VERSION}.tar.gz")
set(ATF_AVRDUDE_SOURCE_SHA256
  72fbe49d3e3ea2f48a750e7f2c16287b163a580e020f745af39d45ba68d9d6ae)
set(ATF_AVRDUDE_LICENSE_URL
  "https://raw.githubusercontent.com/avrdudes/avrdude/v${ATF_AVRDUDE_VERSION}/COPYING")
set(ATF_AVRDUDE_LICENSE_SHA256
  8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643)

# Downloads once into the build tree and verifies the pinned hash.
function(atf_download_checked url destination sha256)
  if(EXISTS "${destination}")
    file(SHA256 "${destination}" actual)
    if(actual STREQUAL sha256)
      return()
    endif()
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${destination}"
    EXPECTED_HASH SHA256=${sha256}
    TLS_VERIFY ON
    STATUS status)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    list(GET status 1 reason)
    file(REMOVE "${destination}")
    message(FATAL_ERROR "Download of ${url} failed: ${reason}")
  endif()
endfunction()

# Places avrdude.exe, avrdude.conf, the license and source in destination.
function(atf_bundle_avrdude destination)
  set(cache "${CMAKE_BINARY_DIR}/_downloads")
  set(binary "${cache}/avrdude-v${ATF_AVRDUDE_VERSION}-windows-x64.zip")
  set(source "${cache}/avrdude-v${ATF_AVRDUDE_VERSION}-source.tar.gz")
  set(license "${cache}/avrdude-v${ATF_AVRDUDE_VERSION}-COPYING")
  atf_download_checked("${ATF_AVRDUDE_BINARY_URL}" "${binary}"
    ${ATF_AVRDUDE_BINARY_SHA256})
  atf_download_checked("${ATF_AVRDUDE_SOURCE_URL}" "${source}"
    ${ATF_AVRDUDE_SOURCE_SHA256})
  atf_download_checked("${ATF_AVRDUDE_LICENSE_URL}" "${license}"
    ${ATF_AVRDUDE_LICENSE_SHA256})

  set(unpack "${cache}/avrdude-v${ATF_AVRDUDE_VERSION}-windows-x64")
  if(NOT EXISTS "${unpack}/avrdude.exe")
    file(REMOVE_RECURSE "${unpack}")
    file(ARCHIVE_EXTRACT INPUT "${binary}" DESTINATION "${unpack}")
  endif()
  foreach(required avrdude.exe avrdude.conf)
    if(NOT EXISTS "${unpack}/${required}")
      message(FATAL_ERROR "AVRDUDE archive lacks ${required}")
    endif()
  endforeach()

  file(MAKE_DIRECTORY "${destination}")
  configure_file("${unpack}/avrdude.exe" "${destination}/avrdude.exe" COPYONLY)
  configure_file("${unpack}/avrdude.conf" "${destination}/avrdude.conf"
    COPYONLY)
  configure_file("${license}" "${destination}/COPYING.txt" COPYONLY)
  configure_file("${source}"
    "${destination}/avrdude-v${ATF_AVRDUDE_VERSION}-source.tar.gz" COPYONLY)
  file(WRITE "${destination}/README.txt"
    "AVRDUDE ${ATF_AVRDUDE_VERSION} (${ATF_AVRDUDE_BASE})\n"
    "installs the ATF150x programmer firmware on the Arduino Leonardo.\n"
    "AVRDUDE is free software under the GNU General Public License,\n"
    "version 2 or later; see COPYING.txt. The complete corresponding source\n"
    "code is included as avrdude-v${ATF_AVRDUDE_VERSION}-source.tar.gz.\n")
endfunction()

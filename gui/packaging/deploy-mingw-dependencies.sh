#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
#
# Copies the MinGW runtime DLLs that windeployqt6 does not deploy (for
# example ICU, zlib, zstd, harfbuzz and freetype of MSYS2's Qt build) into a
# staged application directory. Run inside an MSYS2 MINGW64 shell.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 STAGE_DIRECTORY" >&2
    exit 2
fi

stage_dir=$(cd "$1" && pwd)
compiler=$(command -v c++.exe || command -v c++)
compiler_dir=$(dirname "$compiler")

declare -A compiler_dlls=()
while IFS= read -r -d '' candidate; do
    candidate_name=${candidate##*/}
    compiler_dlls["${candidate_name,,}"]=$candidate
done < <(find "$compiler_dir" -maxdepth 1 -type f -iname '*.dll' -print0)

binaries=()
mapfile -d '' -t binaries < <(
    find "$stage_dir" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0
)
if [[ ${#binaries[@]} -eq 0 ]]; then
    echo "No Windows binaries found under $stage_dir" >&2
    exit 1
fi

# MSYS2's ldd.exe resolves the complete transitive dependency graph in one
# native loader pass. This is dramatically faster than repeatedly parsing all
# large Qt DLLs with objdump, and passing every plugin also covers libraries
# that Qt loads dynamically.
dependency_report=$(ldd "${binaries[@]}")
if grep -Fq 'not found' <<< "$dependency_report"; then
    grep -F 'not found' <<< "$dependency_report" >&2
    exit 1
fi

while IFS= read -r dll; do
    [[ -n "$dll" ]] || continue
    source_path=${compiler_dlls["${dll,,}"]:-}
    if [[ -n "$source_path" ]]; then
        deployed_name=${source_path##*/}
        target_path="$stage_dir/$deployed_name"
        if [[ ! -s "$target_path" ]]; then
            cmake -E copy "$source_path" "$target_path"
            echo "Deployed MinGW dependency: $deployed_name"
        fi
    fi
done < <(
    sed -n 's/^[[:space:]]*\([^[:space:]]*\.dll\)[[:space:]]*=>.*/\1/p' \
        <<< "$dependency_report" | sort -fu
)

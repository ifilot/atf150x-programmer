#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 ATF1502 programmer contributors
#
# Builds, tests and stages the Windows GUI under dist/ with every DLL it
# needs, then optionally creates the portable zip and the Inno Setup
# installer. Run from an MSYS2 MINGW64 shell in the repository root:
#
#   gui/packaging/build-dist.sh [--firmware FILE.hex] [--package] [--installer]
#
# Without --firmware the Leonardo firmware is compiled with arduino-cli when
# it is on PATH. Outputs:
#   dist/atf150x-programmer/                          runnable application
#   dist/atf150x-programmer-vX.Y.Z-windows-x86_64.zip           (--package)
#   dist/atf150x-programmer-vX.Y.Z-windows-x86_64-setup.exe   (--installer)

set -euo pipefail

usage() {
    sed -n '5,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

firmware=""
make_package=0
make_installer=0
build_dir="build-gui"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --firmware) firmware=${2:?}; shift 2 ;;
        --package) make_package=1; shift ;;
        --installer) make_installer=1; shift ;;
        --build-dir) build_dir=${2:?}; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown argument: $1" >&2; usage ;;
    esac
done

repo=$(cd "$(dirname "$0")/../.." && pwd)
cd "$repo"
version=$(sed -n 's/.*kVersion\[\] = "\([^"]*\)".*/\1/p' \
    firmware/atf1502_programmer/version.h)
test -n "$version"
echo "Building ATF150x Programmer v$version"

if [[ -z "$firmware" ]]; then
    if command -v arduino-cli >/dev/null 2>&1; then
        arduino-cli compile --fqbn arduino:avr:leonardo \
            --output-dir build-firmware firmware/atf1502_programmer
        firmware=build-firmware/atf1502_programmer.ino.hex
    else
        echo "arduino-cli not found; pass --firmware FILE.hex" >&2
        exit 1
    fi
fi
test -s "$firmware"
firmware=$(cd "$(dirname "$firmware")" && pwd)/$(basename "$firmware")

cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DATF_BUILD_GUI=ON \
    -DATF_FIRMWARE_HEX="$firmware"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

stage=dist/atf150x-programmer
rm -rf "$stage"
mkdir -p "$stage"
install -m 0755 "$build_dir/gui/atf150x-programmer.exe" "$stage/"
install -m 0755 "$build_dir/atfprog.exe" "$stage/"
cp -R "$build_dir/gui/firmware" "$stage/"
cp -R "$build_dir/gui/tools" "$stage/"
install -m 0644 LICENSE "$stage/LICENSE.txt"
install -m 0644 README.md "$stage/README.md"
install -m 0644 CHANGELOG.md "$stage/CHANGELOG.md"
install -m 0644 docs/THIRD_PARTY.md "$stage/THIRD_PARTY.md"
echo "$version" > "$stage/version.txt"

windeployqt6 --release --compiler-runtime --no-opengl-sw \
    --no-translations --no-system-d3d-compiler \
    --include-plugins qmodernwindowsstyle \
    --dir "$stage" "$stage/atf150x-programmer.exe"
bash gui/packaging/deploy-mingw-dependencies.sh "$stage"

# The staged tree must run without MSYS2 on PATH.
file "$stage/atf150x-programmer.exe" | grep -Fq 'PE32+ executable'
for required in Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6SerialPort.dll \
    Qt6Network.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll \
    platforms/qwindows.dll styles/qmodernwindowsstyle.dll \
    firmware/atf150x-leonardo-firmware.hex tools/avrdude/avrdude.exe \
    tools/avrdude/avrdude.conf; do
    test -s "$stage/$required" || { echo "Missing $required" >&2; exit 1; }
done
echo "Staged application in $stage"

if [[ $make_package -eq 1 ]]; then
    archive="atf150x-programmer-v${version}-windows-x86_64.zip"
    rm -f "dist/$archive"
    (cd dist && cmake -E tar cf "$archive" --format=zip atf150x-programmer)
    echo "Created dist/$archive"
fi

if [[ $make_installer -eq 1 ]]; then
    iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
    test -x "$iscc" || { echo "Inno Setup 6 not found at $iscc" >&2; exit 1; }
    MSYS2_ARG_CONV_EXCL='*' "$iscc" /Q \
        "/DAppVersion=$version" \
        "/DStageDir=$(cygpath -w "$repo/$stage")" \
        "/DOutputDir=$(cygpath -w "$repo/dist")" \
        "$(cygpath -w gui/packaging/setup.iss)"
    echo "Created dist/atf150x-programmer-v${version}-windows-x86_64-setup.exe"
fi

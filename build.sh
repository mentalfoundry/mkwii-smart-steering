#!/usr/bin/env bash
# Build Code.pul for the Riivolution patch. Run from Git Bash.
#
# Needs the CodeWarrior command-line tools in tools/cw/ and the .NET SDK
# (8 or newer); see docs/BUILDING.md. The Kamek linker is compiled from
# tools/kamek-src/ into tools/kamek/ the first time.
#
#   ./build.sh          release build
#   ./build.sh debug    adds OSReport logging (Dolphin: enable the OSREPORT log)
set -euo pipefail
cd "$(dirname "$0")"

CC=tools/cw/mwcceppc.exe
KAMEK=tools/kamek/Kamek.exe
OUT=dist/MKWiiSmartSteering/Binaries

if [ ! -f "$CC" ]; then
    echo "CodeWarrior not found at $CC (see docs/BUILDING.md)" >&2
    exit 1
fi

if [ ! -f "$KAMEK" ]; then
    if ! command -v dotnet >/dev/null; then
        echo "The .NET SDK is needed to build the Kamek linker (see docs/BUILDING.md)" >&2
        exit 1
    fi
    echo "Building the Kamek linker..."
    dotnet build tools/kamek-src/Kamek.csproj -c Release -o tools/kamek -v quiet -nologo > /dev/null
fi

DEFINES=()
if [ "${1:-}" = "debug" ]; then
    DEFINES+=(-DMKWIISS_DEBUG)
fi

CFLAGS=(-I- -i include -Cpp_exceptions off -RTTI off -enum int -opt all -inline auto
        -fp hard -sdata 0 -sdata2 0 -func_align 4 -maxerrors 1)

rm -rf build
mkdir -p build "$OUT"

objects=()
for src in src/*.cpp; do
    obj="build/$(basename "${src%.cpp}").o"
    echo "CC  $src"
    "$CC" "${CFLAGS[@]}" "${DEFINES[@]}" -c -o "$obj" "$src"
    objects+=("$obj")
done

echo "LD  $OUT/Code.pul"
"$KAMEK" "${objects[@]}" -dynamic -externals=externals.txt -versions=versions.txt \
    -output-combined="$OUT/Code.pul" > build/link.log

echo "Done. Copy the contents of dist/ to the root of your SD card, or to Dolphin's Load/Riivolution folder."

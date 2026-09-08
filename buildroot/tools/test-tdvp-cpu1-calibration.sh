#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
stage="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision/stage-calibration.sh"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
fixture="$scratch/mpp/userapps/src/sensor/config"
mkdir -p "$fixture"
files=(gc2093-1920x1080.xml gc2093-1920x1080_auto.json gc2093-1920x1080_manual.json)
for file in "${files[@]}"; do
    # Packaging fixture, not valid calibration or a claim of camera frames.
    printf 'calibration fixture: %s\n\n' "$file" > "$fixture/$file"
done
bash "$stage" "$scratch/mpp" "$scratch/romfs"
for file in "${files[@]}"; do cmp "$fixture/$file" "$scratch/romfs/bin/$file"; done
for file in "${files[@]}"; do
    mv "$fixture/$file" "$scratch/saved"
    if bash "$stage" "$scratch/mpp" "$scratch/missing" > "$scratch/failure.log" 2>&1; then
        echo "FAIL missing $file was accepted" >&2; exit 1
    fi
    grep -Fq "missing or empty: $file" "$scratch/failure.log"
    [ ! -e "$scratch/missing/bin" ]
    : > "$fixture/$file"
    if bash "$stage" "$scratch/mpp" "$scratch/empty" > "$scratch/failure.log" 2>&1; then
        echo "FAIL empty $file was accepted" >&2; exit 1
    fi
    grep -Fq "missing or empty: $file" "$scratch/failure.log"
    [ ! -e "$scratch/empty/bin" ]
    mv "$scratch/saved" "$fixture/$file"
done
if [ "$#" -eq 1 ]; then
    bash "$stage" "$1" "$scratch/pinned-romfs"
    for file in "${files[@]}"; do
        cmp "$1/userapps/src/sensor/config/$file" "$scratch/pinned-romfs/bin/$file"
    done
elif [ "$#" -gt 1 ]; then
    echo "usage: $0 [pinned-mpp-source]" >&2; exit 2
fi
echo 'PASS CPU1 calibration packaging and six missing/empty-asset rejection cases'

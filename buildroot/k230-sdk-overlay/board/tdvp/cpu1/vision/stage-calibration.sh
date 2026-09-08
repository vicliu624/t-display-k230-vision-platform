#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <pinned-mpp-source> <fresh-romfs-root>" >&2; exit 2; }
source_dir="$1/userapps/src/sensor/config"
romfs="$2"
# The firmware builder has verified the SDK commit. Keep its sensor tuning
# byte-for-byte and include only the configured GC2093 1920x1080 profile.
# MPI resolves /bin/<sensor>.xml and matching _auto/_manual JSON files.
files=(gc2093-1920x1080.xml gc2093-1920x1080_auto.json gc2093-1920x1080_manual.json)
for file in "${files[@]}"; do
    if [ ! -f "$source_dir/$file" ] || [ ! -s "$source_dir/$file" ]; then
        echo "FAIL CPU1 calibration missing or empty: $file" >&2
        exit 1
    fi
done
mkdir -p "$romfs/bin"
for file in "${files[@]}"; do
    install -m 0644 "$source_dir/$file" "$romfs/bin/$file"
    cmp "$source_dir/$file" "$romfs/bin/$file"
done
echo 'PASS CPU1 ROMFS: pinned GC2093 XML and auto/manual JSON staged byte-for-byte'

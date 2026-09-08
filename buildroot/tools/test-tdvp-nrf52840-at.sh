#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -f "$test_dir/tdvp-nrf52840"; rmdir "$test_dir"' EXIT
src="$PROJECT_DIR/user-space/vicliu-pocket-linux-hardware/src"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
    "$src/hardware/nrf52840.cpp" "$src/nrf52840-main.cpp" -o "$test_dir/tdvp-nrf52840"
python3 "$SCRIPT_DIR/test-tdvp-nrf52840-at.py" "$test_dir/tdvp-nrf52840"

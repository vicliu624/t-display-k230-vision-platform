#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -UNDEBUG \
    -I"$project/user-space/vicliu-pocket-linux-hardware/src/hardware" \
    "$project/buildroot/tests/keyboard-backlight-test.cpp" -o "$temporary/controls"
"$temporary/controls"
python3 - "$project" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
hardware = root / 'user-space/vicliu-pocket-linux-hardware/src/hardware'
service = (hardware / 'quick_settings_service.cpp').read_text()
assert service.index('(void)initialise_keyboard_backlight();') < service.index('listener_ = socket(')
assert 'key == "keyboard-backlight"' in service
assert '"keyboard-brightness"' in service
status = (hardware / 'status.cpp').read_text()
assert 'get_control("keyboard-brightness", &keyboard_backlight_percent)' in status
patch = (root / 'buildroot/k230-sdk-overlay/linux/0044-tdvp-riscv-dts-canaan-add-keyboard-backlight.patch').read_text()
assert 'pins = "io52";' in patch and 'pins = <52>;' not in patch
assert 'brightness-levels = <255 247 239 231 215 191 159 127 95 47 0>;' in patch
print('keyboard-backlight integration: PASS startup, Quick Settings/status and DT pin binding; not physical brightness acceptance')
PY

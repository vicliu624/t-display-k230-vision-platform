#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    printf 'Usage: %s <real-rm69a10.dtb> <host-tools-bin>\n' "$0" >&2
    exit 2
fi
SOURCE_DTB="$1"
TOOLS="$2"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERIFY="${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/verify-uart1-dtb.sh"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
bash "$VERIFY" "$SOURCE_DTB" "$TOOLS/fdtget"

reject_mutation() {
    local label="$1" type="$2" node="$3" property="$4" value="$5"
    cp "$SOURCE_DTB" "$TEMP_DIR/board.dtb"
    "$TOOLS/fdtput" -t "$type" "$TEMP_DIR/board.dtb" "$node" "$property" "$value"
    if bash "$VERIFY" "$TEMP_DIR/board.dtb" "$TOOLS/fdtget" > "$TEMP_DIR/log" 2>&1; then
        printf 'FAIL: UART1 guard accepted %s\n' "$label" >&2
        exit 1
    fi
}

reject_mutation 'disabled UART' s /soc/serial@91401000 status disabled
reject_mutation 'wrong alias' s /aliases serial1 /soc/serial@91402000
reject_mutation 'unreferenced pinctrl' x /soc/serial@91401000 pinctrl-0 deadbeef
reject_mutation 'wrong TX pin' s /soc/iomux@91105000/tdvp-nrf52840-pins/tx pins io5
reject_mutation 'wrong RX function' s /soc/iomux@91105000/tdvp-nrf52840-pins/rx function alt0
reject_mutation 'Linux takes CPU1 UART' s /soc/serial@91403000 status okay
printf '%s\n' 'test-tdvp-uart1-dtb: PASS real DTB and 6 rejected regressions'

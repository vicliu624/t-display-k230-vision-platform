#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    printf 'Usage: %s <rm69a10.dtb> <host-fdtget>\n' "$0" >&2
    exit 2
fi
DTB="$1"
FDTGET="$2"
[ -s "$DTB" ] && [ -x "$FDTGET" ] || {
    printf '%s\n' 'TDVP UART1 DTB guard: missing DTB or host fdtget' >&2
    exit 1
}
UART=/soc/serial@91401000
PINS=/soc/iomux@91105000/tdvp-nrf52840-pins

require_value() {
    local type="$1" node="$2" property="$3" expected="$4" actual
    actual="$("$FDTGET" -t "$type" "$DTB" "$node" "$property")"
    [ "$actual" = "$expected" ] || {
        printf 'TDVP UART1 DTB guard: %s/%s is "%s", expected "%s"\n' \
            "$node" "$property" "$actual" "$expected" >&2
        exit 1
    }
}

# Raw string presence is insufficient: disabled nodes also contain the pin
# names, and a pin group not referenced by UART1 never configures the pins.
require_value s /aliases serial1 "$UART"
require_value s "$UART" status okay
require_value s "$UART" pinctrl-names default
PIN_PHANDLE="$("$FDTGET" -t x "$DTB" "$PINS" phandle)"
[ -n "$PIN_PHANDLE" ] && [ "$PIN_PHANDLE" != 0 ]
require_value x "$UART" pinctrl-0 "$PIN_PHANDLE"
require_value s "$PINS/tx" pins io3
require_value s "$PINS/tx" function alt3
require_value s "$PINS/rx" pins io4
require_value s "$PINS/rx" function alt3
require_value x "$PINS/tx" output-enable ''
require_value x "$PINS/rx" input-enable ''
require_value s /soc/serial@91400000 status okay
require_value s /soc/serial@91403000 status disabled
printf '%s\n' 'TDVP UART1 DTB guard: PASS UART1 enabled, GPIO3/4 bound, CPU0/CPU1 console ownership retained'

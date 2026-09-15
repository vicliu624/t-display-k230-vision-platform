#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <rm69a10.dtb> <host-fdtget>" >&2; exit 2; }
camera_dtb="$1"
camera_fdtget="$2"
require_value() {
    local type="$1" node="$2" property="$3" expected="$4" actual
    actual="$("$camera_fdtget" -t "$type" "$camera_dtb" "$node" "$property")"
    [ "$actual" = "$expected" ] || {
        echo "TDVP camera DTB: $node/$property: expected '$expected', got '$actual'" >&2
        exit 1
    }
}
phandle() { "$camera_fdtget" -t x "$camera_dtb" "$1" phandle; }
i2c=/soc/i2c@91409000
pins=/soc/iomux@91105000
clocks=/soc/sysctl/sysctl_clock@91100000
divider=$clocks/tdvp_sensor_mclk1
mux=$clocks/tdvp_sensor_mclk1_mux
mipi=/soc/mipi.2
require_value s /aliases i2c0 /soc/i2c@91408000
require_value s /aliases i2c1 /i2c-gpio
require_value s /aliases i2c4 "$i2c"
require_value s "$i2c" status okay
require_value x "$i2c" pinctrl-0 "$(phandle "$pins/tdvp-camera-i2c4-pins")"
require_value s "$pins/tdvp-camera-i2c4-pins" pins 'io7 io8'
require_value s "$pins/tdvp-camera-i2c4-pins" function alt2
require_value s "$pins/tdvp-camera-mclk1-pin" pins io13
require_value s "$pins/tdvp-camera-mclk1-pin" function alt1
require_value x "$i2c/gc2093@37" reg 37
require_value s /soc/mipi.0 status disabled
require_value s /soc/mipi.1 status disabled
require_value s "$mipi" status okay
require_value x "$mipi" reg '0 9000a800 0 800'
require_value x "$mipi" pinctrl-0 "$(phandle "$pins/tdvp-camera-mclk1-pin")"
require_value x "$mipi" reset-gpios "$(phandle /soc/gpio@9140b000/gpio-port@0) 15 0"
require_value x "$mipi" clocks "$(phandle "$divider")"
require_value s "$mipi" clock-names sensor
require_value u "$mipi" assigned-clock-rates '23760000 0'
require_value x "$mipi" assigned-clocks "$(phandle "$divider") $(phandle "$mux")"
require_value x "$divider" clocks "$(phandle "$mux")"
require_value x "$divider" clk-rate-reg-offset 6c
require_value u "$divider" clk-rate-reg-div-value-shift 5
require_value x "$divider" clk-rate-reg-div-value-mask 1f
require_value u "$divider" clk-rate-reg-write-enable-bit 31
require_value x "$mux" clk-parent-reg-offset 6c
require_value u "$mux" clk-parent-reg-value-shift 3
require_value x "$mux" clk-parent-reg-value-mask 3
require_value x "$mux" clk-gate-reg-offset 6c
require_value u "$mux" clk-gate-reg-bit-enable 0
echo 'TDVP camera DTB: PASS physical I2C4, CSI2 and split managed MCLK (not physical sensor acceptance)'

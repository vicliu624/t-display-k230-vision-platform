#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <real-rm69a10.dtb> <host-tools-bin>" >&2; exit 2; }
source_dtb="$1"
camera_tools="$2"
project="$(cd "$(dirname "$0")/../.." && pwd)"
verify="$project/buildroot/k230-sdk-overlay/board/tdvp/verify-camera-dtb.sh"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
bash "$verify" "$source_dtb" "$camera_tools/fdtget"
reject_mutation() {
    local label="$1" type="$2" node="$3" property="$4" value="$5"
    cp "$source_dtb" "$test_dir/board.dtb"
    "$camera_tools/fdtput" -t "$type" "$test_dir/board.dtb" "$node" "$property" "$value"
    if bash "$verify" "$test_dir/board.dtb" "$camera_tools/fdtget" > "$test_dir/log" 2>&1; then
        echo "FAIL: camera DTB guard accepted $label" >&2
        exit 1
    fi
}
reject_mutation 'wrong camera bus' s /aliases i2c4 /soc/i2c@91408000
reject_mutation 'disabled I2C4' s /soc/i2c@91409000 status disabled
reject_mutation 'wrong I2C4 function' s /soc/iomux@91105000/tdvp-camera-i2c4-pins function alt0
reject_mutation 'two CSI owners' s /soc/mipi.0 status okay
reject_mutation 'disabled CSI2' s /soc/mipi.2 status disabled
reject_mutation 'unbound clock' x /soc/mipi.2 clocks deadbeef
reject_mutation 'unguarded rate' u /soc/mipi.2 assigned-clock-rates 594000000
reject_mutation 'wrong divider' u /soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1 clk-rate-reg-div-value-shift 0
reject_mutation 'wrong physical gate' u /soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1_mux clk-gate-reg-bit-enable 1
reject_mutation 'unbound reset' x /soc/mipi.2 reset-gpios deadbeef
echo 'TDVP camera DTB: PASS real board and 10 rejected configuration regressions'

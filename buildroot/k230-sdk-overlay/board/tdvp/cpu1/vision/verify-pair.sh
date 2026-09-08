#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 3 ] || { echo "usage: $0 <linux.dtb> <fdtget> <cpu1.manifest>" >&2; exit 2; }
dtb="$1"
fdtget="$2"
manifest="$3"
fail() { echo "TDVP AI/vision pair: $*" >&2; exit 1; }
value() {
    local actual
    actual="$("$fdtget" -t "$1" "$dtb" "$2" "$3")" || fail "missing $2/$3"
    [ "$actual" = "$4" ] || fail "$2/$3 expected '$4', got '$actual'"
}
handle() { "$fdtget" -t x "$dtb" "$1" phandle; }
absent() {
    if "$fdtget" "$dtb" "$1" "$2" >/dev/null 2>&1; then fail "retained $1/$2"; fi
}
for field in 'resource_owner=cpu1-ai-vision' 'ownership_contract=2' \
    'mmz_base=0x14000000' 'mmz_size=0x08000000' \
    'transport_base=0x1c000000' 'transport_size=0x02000000' \
    'camera=gc2093-csi2' 'ai_engines=gnne,ai2d,fft' \
    'ai_job_abi=1' 'ai_job_backend=ai2d,fft' 'ai_job_control=0x1dff2000'; do
    [ "$(grep -c "^${field%%=*}=" "$manifest")" -eq 1 ] || fail "missing/duplicate firmware key: ${field%%=*}"
    [ "$(grep -Fxc "$field" "$manifest")" -eq 1 ] || fail "missing/duplicate firmware field: $field"
done
clock=/soc/sysctl/sysctl_clock@91100000
pll=/soc/sysctl/sysctl_boot@91102000
power=/soc/sysctl/sysctl_power@91103000
gpio=/soc/gpio@9140b000
mmz=/reserved-memory/cpu1-mmz@14000000
transport=/reserved-memory/cpu1-transport@1c000000
value x "$mmz" reg '0 14000000 0 8000000'
value x "$transport" reg '0 1c000000 0 2000000'
for region in "$mmz" "$transport"; do
    value x "$region" no-map ''
    absent "$region" reusable
done
value x /reserved-memory/cpu1-runtime@10000000 reg '0 10000000 0 4000000'
value x /cpu1-mailbox@13ff0000 reg '0 13ff0000 0 10000'
value s /cpu1-vision compatible tdvp,cpu1-vision-v1
value s /cpu1-vision status okay
value s /cpu1-vision memory-region-names 'transport mmz'
value x /cpu1-vision memory-region "$(handle "$transport") $(handle "$mmz")"
value s /cpu1-vision clock-names 'pll0 pll1 pll2 isp-ddr ai-ddr'
value x /cpu1-vision clocks "$(handle "$pll/pll0_div4") $(handle "$pll/pll1_div4") $(handle "$pll/pll2_div4") $(handle "$clock/tdvp_isp_ddr") $(handle "$clock/tdvp_ai_ddr")"
value s /cpu1-vision power-domain-names 'ai disp'
value x /cpu1-vision power-domains "$(handle "$power") 1 $(handle "$power") 2"
value x /cpu1-vision tdvp,gpio-controller "$(handle "$gpio")"
value x /cpu1-vision tdvp,power-controller "$(handle "$power")"
value x /cpu1-vision tdvp,clock-controller "$(handle "$clock")"
absent /cpu1-vision assigned-clocks
value x "$gpio" tdvp,cpu1-gpio-mask 200000
value u "$gpio/gpio-port@0" gpio-reserved-ranges '21 1'
value x "$power" tdvp,cpu1-vision-domains ''
value x "$clock" tdvp,cpu1-i2c4-clock-sharing ''
for port in tdvp_isp_ddr tdvp_ai_ddr; do
    bit=4; [ "$port" != tdvp_ai_ddr ] || bit=6
    value s "$clock/$port" compatible canaan,k230-clk-composite
    value s "$clock/$port" status okay
    value x "$clock/$port" clocks "$(handle "$pll/pll0_div4")"
    value x "$clock/$port" clk-gate-reg-offset 60
    value u "$clock/$port" clk-gate-reg-bit-enable "$bit"
    value u "$clock/$port" clk-gate-reg-bit-reverse 0
    value u "$clock/$port" read-only 0
    for prop in clk-rate-reg-offset clk-rate-reg-offset_1 clk-parent-reg-offset assigned-clocks; do
        absent "$clock/$port" "$prop"
    done
done
for node in /soc/i2c@91409000 /soc/i2c@91409000/gc2093@37 /soc/isp.0 \
    /soc/mipi.0 /soc/mipi.1 /soc/mipi.2 /soc/gnne@80400000 /soc/ai2d@80400c00 \
    "$clock/i2c4_clk" "$clock/i2c4_pclk_gate" "$clock/tdvp_sensor_mclk1" \
    "$clock/tdvp_sensor_mclk1_mux" "$clock/ai_clk" "$clock/ai_aclk"; do
    value s "$node" status disabled
done
for prop in pinctrl-0 pinctrl-names; do absent /soc/i2c@91409000 "$prop"; done
for prop in reset-gpios pinctrl-0 pinctrl-names clocks clock-names assigned-clocks assigned-clock-parents assigned-clock-rates; do
    absent /soc/mipi.2 "$prop"
done
echo 'TDVP AI/vision pair: PASS CPU1 contract-2 firmware and Linux ownership DT; not driver/hardware acceptance'

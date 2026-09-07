#!/usr/bin/env bash
set -euo pipefail
[ "$#" -ge 2 ] && [ "$#" -le 3 ] || { echo "usage: $0 <cpu0-camera.dtb> <cpu1-vision.dtb> [real-cpu1.manifest]" >&2; exit 2; }
project="$(cd "$(dirname "$0")/../.." && pwd)"
guard="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision/verify-pair.sh"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
baseline="$1"; candidate="$2"
fdtget="${FDTGET:-fdtget}"; fdtput="${FDTPUT:-fdtput}"
if [ "$#" -eq 3 ]; then
    cp "$3" "$test_dir/manifest"
else
    # Guard fixture only. It makes no assertion that these bytes are firmware.
    printf '%s\n' 'resource_owner=cpu1-ai-vision' 'ownership_contract=2' \
        'mmz_base=0x14000000' 'mmz_size=0x08000000' \
        'transport_base=0x1c000000' 'transport_size=0x02000000' \
        'camera=gc2093-csi2' 'ai_engines=gnne,ai2d,fft' > "$test_dir/manifest"
fi
bash "$guard" "$candidate" "$fdtget" "$test_dir/manifest"
if bash "$guard" "$baseline" "$fdtget" "$test_dir/manifest" > "$test_dir/rejected.log" 2>&1; then
    echo 'FAIL CPU0 camera DT accepted with CPU1 AI firmware' >&2; exit 1
fi
rejections=1
while IFS= read -r field; do
    case "$field" in resource_owner=*|ownership_contract=*|mmz_base=*|mmz_size=*|transport_base=*|transport_size=*|camera=*|ai_engines=*) ;; *) continue ;; esac
    grep -Fvx "$field" "$test_dir/manifest" > "$test_dir/mutated.manifest"
    if bash "$guard" "$candidate" "$fdtget" "$test_dir/mutated.manifest" > "$test_dir/rejected.log" 2>&1; then
        echo "FAIL missing manifest field accepted: $field" >&2; exit 1
    fi
    rejections=$((rejections+1))
    cp "$test_dir/manifest" "$test_dir/mutated.manifest"
    printf '%s=conflicting\n' "${field%%=*}" >> "$test_dir/mutated.manifest"
    if bash "$guard" "$candidate" "$fdtget" "$test_dir/mutated.manifest" > "$test_dir/rejected.log" 2>&1; then
        echo "FAIL conflicting duplicate manifest key accepted: $field" >&2; exit 1
    fi
    rejections=$((rejections+1))
done < "$test_dir/manifest"
for mutation in \
    's /soc/gnne@80400000 status okay' \
    's /soc/ai2d@80400c00 status okay' \
    's /soc/i2c@91409000 status okay' \
    's /soc/mipi.2 status okay' \
    's /soc/isp.0 status okay' \
    'x /soc/mipi.2 assigned-clocks 0' \
    'x /soc/i2c@91409000 pinctrl-0 0' \
    'x /reserved-memory/cpu1-mmz@14000000 reg 0 14000000 0 10000000' \
    'x /reserved-memory/cpu1-transport@1c000000 reg 0 1b000000 0 2000000' \
    'x /soc/gpio@9140b000 tdvp,cpu1-gpio-mask 0' \
    'x /cpu1-vision clocks 0' \
    'x /cpu1-vision power-domains 0' \
    'x /soc/sysctl/sysctl_clock@91100000/tdvp_ai_ddr clk-gate-reg-bit-enable 5' \
    's /soc/sysctl/sysctl_clock@91100000/ai_clk status okay'; do
    read -r -a fields <<< "$mutation"
    cp "$candidate" "$test_dir/mutated.dtb"
    "$fdtput" -t "${fields[0]}" "$test_dir/mutated.dtb" "${fields[@]:1}"
    if bash "$guard" "$test_dir/mutated.dtb" "$fdtget" "$test_dir/manifest" > "$test_dir/rejected.log" 2>&1; then
        echo "FAIL mixed ownership accepted: $mutation" >&2; exit 1
    fi
    rejections=$((rejections+1))
done
echo "CPU1 packaging pair: PASS real compiled DTBs and $rejections mixed/missing-contract refusals"

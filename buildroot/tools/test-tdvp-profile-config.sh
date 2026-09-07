#!/usr/bin/env bash
set -euo pipefail
[ "$#" -le 1 ] || { echo 'usage: test-tdvp-profile-config.sh [real-buildroot.config]' >&2; exit 2; }
project="$(cd "$(dirname "$0")/../.." && pwd)"
baseline="$project/buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh"
profile="$project/buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT

# Exercise the production lists and helper, not a second copy of the rules.
# The complete staging entry point is separately exercised on the real SDK.
for declaration in required_config selected_config; do
    source <(sed -n "/^${declaration}=(/,/^)/p" "$baseline")
done
for helper in fail require_line require_profile_config; do
    source <(sed -n "/^${helper}() {/,/^}/p" "$baseline")
    declare -F "$helper" >/dev/null
done
[ "${#required_config[@]}" -gt 0 ]
[ "${#selected_config[@]}" -gt 0 ]

require_profile_config "$profile" source
grep -Eq '^[[:space:]]*select BR2_PACKAGE_TDVP_CAMERA_ISP_RUNTIME$' \
    "$project/buildroot/k230-sdk-overlay/package/tdvp-camera-isp/Config.in"
! grep -Fqx 'BR2_PACKAGE_TDVP_CAMERA_ISP_RUNTIME=y' "$profile"
# The source config itself must not masquerade as a resolved configuration.
if (require_profile_config "$profile" resolved) > "$temporary/rejected.log" 2>&1; then
    echo 'FAIL: unresolved source config accepted as resolved' >&2
    exit 1
fi
grep -Fq 'BR2_PACKAGE_TDVP_CAMERA_ISP_RUNTIME=y' "$temporary/rejected.log"

cp "$profile" "$temporary/resolved.config"
printf '%s=y\n' "${selected_config[@]}" >> "$temporary/resolved.config"
require_profile_config "$temporary/resolved.config" resolved
if [ "$#" -eq 1 ]; then
    require_profile_config "$1" resolved
    echo 'TDVP profile config: PASS real Kconfig output'
fi

# Every required product selection and hidden dependency must still fail
# closed if dropped; none may disappear as a side effect of phase separation.
rejections=0
for symbol in "${required_config[@]}" "${selected_config[@]}"; do
    sed "/^${symbol}=y$/d" "$temporary/resolved.config" > "$temporary/mutated.config"
    if (require_profile_config "$temporary/mutated.config" resolved) > "$temporary/rejected.log" 2>&1; then
        echo "FAIL: missing $symbol accepted in resolved config" >&2
        exit 1
    fi
    grep -Fq "$symbol=y" "$temporary/rejected.log"
    rejections=$((rejections + 1))
done
sed '/^BR2_PACKAGE_TDVP_CAMERA_ISP=y$/d' "$profile" > "$temporary/mutated.config"
if (require_profile_config "$temporary/mutated.config" source) > "$temporary/rejected.log" 2>&1; then
    echo 'FAIL: source profile without the camera package accepted' >&2
    exit 1
fi
grep -Fq 'BR2_PACKAGE_TDVP_CAMERA_ISP=y' "$temporary/rejected.log"
if (require_profile_config "$profile" invalid) > "$temporary/rejected.log" 2>&1; then
    echo 'FAIL: invalid assertion phase accepted' >&2
    exit 1
fi
echo "TDVP profile config: PASS source/resolved separation; $rejections missing selections rejected"

#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo 'Usage: test-tdvp-sdk-relocation.sh <cpu0-sdk.tar.gz> <release-bundle>' >&2
    exit 2
fi
archive="$(realpath "$1")"
bundle="$(realpath "$2")"
test_image="${TDVP_SDK_TEST_IMAGE:-tdvp-sdk-validation:ubuntu24.04}"
script_dir="$(cd "$(dirname "$0")" && pwd)"
command -v docker >/dev/null
[[ "$(dirname "$archive")" == "$bundle" ]] || { echo 'SDK archive must be inside its release bundle' >&2; exit 1; }
expected_archive="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["archive"])' "$bundle/tdvp-sdk-manifest.json")"
[[ "$(basename "$archive")" == "$expected_archive" ]] || { echo 'SDK archive name differs from release manifest' >&2; exit 1; }
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
# Input is the locally generated archive, checked against the collected release.
(cd "$bundle" && sha256sum --strict -c SHA256SUMS)
tar -xzf "$archive" --no-same-owner -C "$temporary"
sdk="$temporary/tdvp-sdk"
[[ -f "$sdk/tdvp-sdk-manifest.json" ]]
cmp "$sdk/tdvp-sdk-manifest.json" "$bundle/tdvp-sdk-manifest.json"
for location in /sdk /another/location/with-a-longer-name/tdvp-sdk; do
    docker run --rm --network none --user 1000:1000 --tmpfs /opt \
        --mount "type=bind,src=$sdk,dst=$location,readonly" \
        --mount "type=bind,src=$bundle,dst=/release,readonly" \
        --mount "type=bind,src=$script_dir/test-tdvp-sdk-builds.py,dst=/sdk-consumer-checks.py,readonly" \
        "$test_image" env -i PATH=/usr/bin:/bin HOME=/tmp LC_ALL=C SDK_UNDER_TEST="$location" \
        bash -c '
            set -euo pipefail
            test ! -e /opt/toolchain
            test ! -e /validation/sdk
            source "$SDK_UNDER_TEST/environment-setup.sh"
            test "$($CC -print-sysroot)" = "$SDK_UNDER_TEST/sysroot"
            python3 "$SDK_UNDER_TEST/verify-sdk.py" "$SDK_UNDER_TEST" --bundle /release --smoke
            python3 /sdk-consumer-checks.py "$SDK_UNDER_TEST"
        '
done
printf '%s\n' 'TDVP SDK relocation: PASS two paths, ordinary UID, read-only SDK, no network or original toolchain/build tree'

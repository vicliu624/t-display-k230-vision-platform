#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
patch="$project/buildroot/k230-sdk-overlay/linux/0067-tdvp-gpio-cpu1-shared-port-arbitration.patch"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
# Test the production patch's implementation, not a parallel fixture copy.
awk '
    /^diff --git / { selected = ($0 == "diff --git a/drivers/gpio/gpio-k230-tdvp-amp.h b/drivers/gpio/gpio-k230-tdvp-amp.h"); next }
    selected && /^\+\+\+/ { next }
    selected && /^\+/ { print substr($0, 2) }
' "$patch" > "$test_dir/gpio-k230-tdvp-amp.h"
test -s "$test_dir/gpio-k230-tdvp-amp.h"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-misleading-indentation -O2 -UNDEBUG -pthread \
    -I"$test_dir" "$project/buildroot/tools/tests/tdvp-cpu1-gpio-amp-test.c" -o "$test_dir/check"
"$test_dir/check"

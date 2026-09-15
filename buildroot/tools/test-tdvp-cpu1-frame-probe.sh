#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -I"$vision" \
    "$project/buildroot/tools/tests/tdvp-cpu1-frame-probe-test.c" -o "$temporary/test"
"$temporary/test"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$vision" \
    "$project/buildroot/tools/tdvp-cpu1-frame-probe.c" -o "$temporary/probe"
# Invalid CLI input must fail before opening any device.
if "$temporary/probe" 0 1000 > "$temporary/rejected.log" 2>&1; then
    echo 'FAIL invalid frame count accepted' >&2; exit 1
else
    test "$?" -eq 2
fi
grep -Fq 'usage:' "$temporary/rejected.log"

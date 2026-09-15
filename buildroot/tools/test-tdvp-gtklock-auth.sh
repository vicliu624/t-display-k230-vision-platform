#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="${1:?usage: test-tdvp-gtklock-auth.sh <gtklock-4.0.0-source> [PAM-security-header-directory]}"
pam_headers="${2:-/usr/include/security}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
test -f "$source_dir/include/auth.h"
test -f "$pam_headers/pam_appl.h"
mkdir -p "$test_dir/include/security"
cp "$pam_headers/"*.h "$test_dir/include/security/"
# Upstream auth.h includes glib.h but its enum-only API uses no GLib types.
# Do not pull cross-sysroot libc headers into this native fault-injection test.
printf '/* No GLib symbols used by the authentication API. */\n' > "$test_dir/include/glib.h"
backend="$project/buildroot/k230-sdk-overlay/package/gtklock/src/tdvp-auth.c"
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$test_dir/include" -I"$source_dir/include" \
    -DTDVP_AUTH_SOURCE="\"$backend\"" \
    "$project/buildroot/tools/tests/tdvp-gtklock-auth-test.c" -o "$test_dir/test-auth"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$test_dir/test-auth"

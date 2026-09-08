#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
flags=()
if [ "$#" -eq 1 ]; then
    # Compile the fixed SDK's real conversion, not a success-only mock.
    awk '/^#define IMPL_POLLIN/{copy=1} /^int sys_poll\(/{copy=0} copy' \
        "$1/components/lwp/lwp_syscall.c" > "$scratch/tdvp-poll-conversion.h"
    grep -q 'static void dfs2musl_events' "$scratch/tdvp-poll-conversion.h"
    flags=(-DTDVP_TEST_PINNED_POLL -I"$scratch")
elif [ "$#" -ne 0 ]; then
    echo "usage: $0 [pinned-rt-thread-source]" >&2; exit 2
fi
"${CC:-cc}" -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -I"$vision" \
    "${flags[@]}" \
    "$vision/tdvp_cpu1_ai_guard.c" "$project/buildroot/tools/tests/tdvp-cpu1-ai-guard-test.c" \
    -o "$scratch/test"
"$scratch/test"

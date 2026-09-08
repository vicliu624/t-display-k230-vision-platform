#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
"${CC:-cc}" -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -I"$vision" \
    "$project/buildroot/tools/tests/tdvp-ai-abi-test.c" -o "$scratch/test"
"$scratch/test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -x c++ -fsyntax-only "$vision/tdvp_ai_abi.h"
cmp "$vision/tdvp_nncase_tls.lds" "$project/buildroot/tools/tdvp-cpu1-nncase-tls.lds"
bash "$project/buildroot/tools/test-tdvp-cpu1-shared-map.sh"
# Compile the actual Linux acceptance client even when no board is attached.
# Only invalid CLI invocations run here; numerical/driver tests require a board.
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$vision" \
    "$project/buildroot/tools/tdvp-cpu1-ai-probe.c" -o "$scratch/probe"
for invalid in 0 101 -1 x 1x ''; do
    result=0
    "$scratch/probe" "$invalid" > "$scratch/rejected.log" 2>&1 || result=$?
    [ "$result" -eq 2 ] || { echo "AI probe accepted invalid count: $invalid" >&2; exit 1; }
done
echo 'AI probe: PASS host compile and six invalid CLI counts (not hardware acceptance)'

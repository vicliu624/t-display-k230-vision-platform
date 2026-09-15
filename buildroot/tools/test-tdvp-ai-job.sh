#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
"${CC:-cc}" -std=c11 -O2 -UNDEBUG -Wall -Wextra -Werror -I"$vision" \
    "$vision/tdvp_ai_job.c" "$project/buildroot/tools/tests/tdvp-ai-job-test.c" -o "$scratch/test"
"$scratch/test"
# Also compile the public header in C++, as required by the nncase executor.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -x c++ -fsyntax-only "$vision/tdvp_ai_job.h"

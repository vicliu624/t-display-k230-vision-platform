#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -DTDVP_CPU1_TEST_EMULATED \
    "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-cpu1-acceptance.c" \
    -o "${TEMP_DIR}/cpu1-mailbox-test"
"${TEMP_DIR}/cpu1-mailbox-test"

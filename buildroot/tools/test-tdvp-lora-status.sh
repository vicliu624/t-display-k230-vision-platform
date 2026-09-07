#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf -- "${TEST_ROOT}"' EXIT
SOURCE="${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/hardware"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"${SOURCE}" \
    "${PROJECT_DIR}/buildroot/tests/tdvp-lora-status-test.cpp" \
    "${SOURCE}/lora_status.cpp" -o "${TEST_ROOT}/lora-status-test"
"${TEST_ROOT}/lora-status-test"

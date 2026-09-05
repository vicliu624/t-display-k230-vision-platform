#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="${SCRIPT_DIR}/../k230-sdk-overlay/linux"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
DTS_PATH='arch/riscv/boot/dts/canaan/k230-canmv-rm69a10.dts'
git -C "${TEMP_DIR}" init -q

# Replay the real, ordered board-DTS edits, not just the initial 0033 file.
# The CPU0/UART3 hunk must apply after radio, keyboard, audio and CPU1 patches.
for patch_file in "${PATCH_DIR}"/*.patch; do
	git -C "${TEMP_DIR}" apply --include="${DTS_PATH}" "${patch_file}"
done
grep -Fq '&uart3 { status = "disabled"; };' "${TEMP_DIR}/${DTS_PATH}"
grep -Fq '&uart0 { status = "okay"; };' "${TEMP_DIR}/${DTS_PATH}"
grep -Fq 'tdvp,k230-cpu1-mailbox' "${TEMP_DIR}/${DTS_PATH}"
printf '%s\n' 'test-tdvp-cpu0-dts-queue: PASS full board queue retains UART0, reserves UART3 and CPU1 mailbox'

#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="${1:?missing Buildroot target directory argument}"
INITTAB="${TARGET_DIR}/etc/inittab"

if [ ! -f "${INITTAB}" ]; then
	echo "TDVP: missing ${INITTAB}" >&2
	exit 1
fi

if ! grep -q '^tty1::respawn:' "${INITTAB}"; then
	cat >> "${INITTAB}" <<'EOF'

# TDVP screen console login on the framebuffer-backed virtual terminal.
tty1::respawn:/sbin/getty -L tty1 0 vt100
EOF
fi

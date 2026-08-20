#!/bin/sh
set -eu

: "${TDVP_REMOTE_PASSWORD:?TDVP_REMOTE_PASSWORD must be set for password authentication}"
printf '%s\n' "${TDVP_REMOTE_PASSWORD}"

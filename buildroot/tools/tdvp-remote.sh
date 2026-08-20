#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOST="${TDVP_REMOTE_HOST:-vicliu.i234.me}"
PORT="${TDVP_REMOTE_PORT:-10022}"
USER_NAME="${TDVP_REMOTE_USER:-root}"
STATE_DIR="${TDVP_REMOTE_STATE_DIR:-${HOME}/.cache/tdvp-remote}"
ASKPASS="${ROOT}/buildroot/tools/tdvp-ssh-askpass.sh"
KNOWN_HOSTS="${STATE_DIR}/known_hosts"

if [ "$#" -eq 0 ]; then
	printf 'usage: TDVP_REMOTE_PASSWORD=... %s <remote-command> [argument ...]\n' "$0" >&2
	printf '       TDVP_REMOTE_PASSWORD=... %s --copy <local-path> <remote-path>\n' "$0" >&2
	exit 2
fi

: "${TDVP_REMOTE_PASSWORD:?TDVP_REMOTE_PASSWORD must be set}"
mkdir -p "${STATE_DIR}"
chmod 0700 "${STATE_DIR}"
chmod 0700 "${ASKPASS}"

export DISPLAY="${DISPLAY:-tdvp-remote}"
export SSH_ASKPASS_REQUIRE=force
export SSH_ASKPASS="${ASKPASS}"

# The host shell commonly has no controlling terminal.  OpenSSH 7.x can then
# wait at password authentication without invoking SSH_ASKPASS.  Starting the
# client in a new session makes the askpass contract deterministic while
# preserving stdin for commands such as the read-only hardware audit.
run_ssh_client() {
	if command -v setsid >/dev/null 2>&1; then
		setsid "$@"
	else
		"$@"
	fi
}

if [ "$1" = "--copy" ]; then
	if [ "$#" -ne 3 ]; then
		printf '%s: --copy requires a local path and a remote path\n' "$0" >&2
		exit 2
	fi
	local_path="$2"
	remote_path="$3"
	if [ ! -e "${local_path}" ]; then
		printf '%s: local path does not exist: %s\n' "$0" "${local_path}" >&2
		exit 2
	fi
	copy_options=(
		-o "UserKnownHostsFile=${KNOWN_HOSTS}"
		-o StrictHostKeyChecking=accept-new
		-o ConnectTimeout=10
		-o ServerAliveInterval=10
		-o ServerAliveCountMax=3
		-P "${PORT}"
	)
	if [ -d "${local_path}" ]; then
		copy_options+=(-r)
	fi
	run_ssh_client scp "${copy_options[@]}" "${local_path}" "${USER_NAME}@${HOST}:${remote_path}"
	exit $?
fi

remote_command="$*"
# PowerShell can preserve a trailing CR when a here-string is piped into WSL.
# Strip it before handing the command to the target shell so paths and unit
# names stay byte-for-byte valid.
remote_command="$(printf '%s' "${remote_command}" | tr -d '\r')"

run_ssh_client ssh \
	-o "UserKnownHostsFile=${KNOWN_HOSTS}" \
	-o StrictHostKeyChecking=accept-new \
	-o ConnectTimeout=10 \
	-o ServerAliveInterval=10 \
	-o ServerAliveCountMax=3 \
	-p "${PORT}" \
	"${USER_NAME}@${HOST}" \
	"${remote_command}"

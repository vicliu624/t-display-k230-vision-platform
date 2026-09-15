#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# Keep the deployment default independent of public DNS.  The previous name
# currently resolves to a different address, which can silently send a
# hardware audit or candidate deployment to the wrong host.  Operators can
# still override this for a deliberately managed DNS name.
HOST="${TDVP_REMOTE_HOST:-182.242.73.144}"
PORT="${TDVP_REMOTE_PORT:-10022}"
USER_NAME="${TDVP_REMOTE_USER:-root}"
STATE_DIR="${TDVP_REMOTE_STATE_DIR:-${HOME}/.cache/tdvp-remote}"
ASKPASS="${ROOT}/buildroot/tools/tdvp-ssh-askpass.sh"
KNOWN_HOSTS="${STATE_DIR}/known_hosts"
# This is the already-attested Ed25519 key for the K230.  Keep the pin on the
# host side: a candidate deploy must not learn a key from whatever endpoint
# currently answers an IP address.  An intentional device re-key requires an
# explicit, independently verified TDVP_REMOTE_HOSTKEY_SHA256 override.
EXPECTED_HOSTKEY_SHA256="${TDVP_REMOTE_HOSTKEY_SHA256:-SHA256:53J1eOjRe6Zw/FStVVOhqOnT7ShXylup7Vc+2c2HTFA}"

if [ "$#" -eq 0 ]; then
	printf 'usage: TDVP_REMOTE_PASSWORD=... %s <remote-command> [argument ...]\n' "$0" >&2
	printf '       TDVP_REMOTE_PASSWORD=... %s --copy <local-path> <remote-path>\n' "$0" >&2
	printf '       Optional: TDVP_REMOTE_HOSTKEY_SHA256=SHA256:... (only after independent key verification)\n' >&2
	exit 2
fi

: "${TDVP_REMOTE_PASSWORD:?TDVP_REMOTE_PASSWORD must be set}"
mkdir -p "${STATE_DIR}"
chmod 0700 "${STATE_DIR}"
chmod 0700 "${ASKPASS}"

export DISPLAY="${DISPLAY:-tdvp-remote}"
export SSH_ASKPASS_REQUIRE=force
export SSH_ASKPASS="${ASKPASS}"

fail() {
	printf '%s\n' "tdvp-remote: ERROR: $*" >&2
	exit 1
}

verify_remote_host_key() {
	command -v ssh-keyscan >/dev/null 2>&1 ||
		fail "ssh-keyscan is required for pinned host-key verification"
	command -v ssh-keygen >/dev/null 2>&1 ||
		fail "ssh-keygen is required for pinned host-key verification"

	case "${EXPECTED_HOSTKEY_SHA256}" in
	SHA256:*) ;;
	*) fail "TDVP_REMOTE_HOSTKEY_SHA256 must be an OpenSSH SHA256 fingerprint" ;;
	esac

	key_scan="$(mktemp "${STATE_DIR}/host-key.XXXXXX")"
	trap 'rm -f "${key_scan}"' RETURN
	# Request only the pinned key type.  Accepting an arbitrary additional key
	# from a scan would defeat the fingerprint pin on the subsequent ssh/scp.
	ssh-keyscan -T 8 -p "${PORT}" -t ed25519 "${HOST}" >"${key_scan}" 2>/dev/null || true
	[ -s "${key_scan}" ] ||
		fail "no Ed25519 SSH host key received from ${HOST}:${PORT}"

	key_lines="$(grep -c '^[^#]' "${key_scan}" || true)"
	[ "${key_lines}" -eq 1 ] ||
		fail "expected exactly one Ed25519 host key from ${HOST}:${PORT}, got ${key_lines}"
	observed_hostkey="$(ssh-keygen -lf "${key_scan}" -E sha256 2>/dev/null | awk 'NR == 1 { print $2 }')"
	[ "${observed_hostkey}" = "${EXPECTED_HOSTKEY_SHA256}" ] ||
		fail "host key mismatch for ${HOST}:${PORT}: expected=${EXPECTED_HOSTKEY_SHA256} actual=${observed_hostkey:-unreadable}"

	# Retain only a fingerprint-verified key. StrictHostKeyChecking=yes below
	# prevents an ssh/scp invocation from accepting a different key later.
	if ! grep -Fqx "$(cat "${key_scan}")" "${KNOWN_HOSTS}" 2>/dev/null; then
		cat "${key_scan}" >>"${KNOWN_HOSTS}"
	fi
	chmod 0600 "${KNOWN_HOSTS}"
	trap - RETURN
	rm -f "${key_scan}"
}

verify_remote_host_key

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
		-o StrictHostKeyChecking=yes
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
	-o StrictHostKeyChecking=yes \
	-o ConnectTimeout=10 \
	-o ServerAliveInterval=10 \
	-o ServerAliveCountMax=3 \
	-p "${PORT}" \
	"${USER_NAME}@${HOST}" \
	"${remote_command}"

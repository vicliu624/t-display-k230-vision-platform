#!/usr/bin/env bash
set -euo pipefail

# Validate the *published* package feed that a release image will use.  The
# firmware embeds this exact public key and requires GPG ASCII-armoured index
# signatures (opkg's "gpg-asc" backend).  Source-level configuration alone is
# not a release proof: the Page can be absent, a branch can publish a different
# ABI, or a signature can be missing.

if [ "$#" -ne 0 ]; then
	printf '%s\n' 'Usage: assert-tdvp-opkg-feed-release.sh' >&2
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FEED_URL='https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r3/riscv64'
PUBLIC_KEY="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-repo-public.asc"
FINGERPRINT='2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500'
ABI_VERSION='2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1'

for command in curl gpg gpgv gzip awk grep mktemp; do
	command -v "${command}" >/dev/null 2>&1 || {
		printf 'TDVP feed release gate: required host command is missing: %s\n' "${command}" >&2
		exit 1
	}
done

[ -s "${PUBLIC_KEY}" ] || {
	printf 'TDVP feed release gate: embedded public key is missing: %s\n' "${PUBLIC_KEY}" >&2
	exit 1
}

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT
gpg_home="${tmpdir}/gnupg"
mkdir -p "${gpg_home}"
chmod 700 "${gpg_home}"

# GnuPG 2.2 (the Ubuntu 18.04 host supported by this SDK) does not have
# --show-keys.  A show-only, dry-run import yields the same colon-formatted
# fingerprint without mutating the user's keyring.
key_fingerprint="$(gpg --batch --homedir "${gpg_home}" --with-colons \
	--import-options show-only --dry-run --import "${PUBLIC_KEY}" 2>/dev/null |
	awk -F: '$1 == "fpr" { print $10; exit }')"
[ "${key_fingerprint}" = "${FINGERPRINT}" ] || {
	printf '%s\n' 'TDVP feed release gate: embedded public key fingerprint does not match the release contract' >&2
	exit 1
}

download() {
	local remote_name="$1"
	local local_name="$2"

	# Do not use curl options newer than the Ubuntu 18.04 host supported by the
	# K230 SDK.  HTTP 404 is intentionally fatal; curl does not retry it.
	curl --fail --location --silent --show-error --proto '=https' --tlsv1.2 \
		--connect-timeout 15 --max-time 90 --retry 3 \
		--output "${tmpdir}/${local_name}" "${FEED_URL}/${remote_name}"
}

download 'Packages.gz' 'Packages.gz'
download 'Packages.asc' 'Packages.asc'
download 'Packages.gz.asc' 'Packages.gz.asc'
download 'release.json' 'release.json'

gpg --batch --homedir "${gpg_home}" --yes --dearmor \
	--output "${tmpdir}/tdvp-release-keyring.gpg" "${PUBLIC_KEY}"
gzip -dc -- "${tmpdir}/Packages.gz" > "${tmpdir}/Packages"
[ -s "${tmpdir}/Packages" ] || {
	printf '%s\n' 'TDVP feed release gate: published Packages index is empty' >&2
	exit 1
}
gpgv --keyring "${tmpdir}/tdvp-release-keyring.gpg" \
	"${tmpdir}/Packages.asc" "${tmpdir}/Packages"
gpgv --keyring "${tmpdir}/tdvp-release-keyring.gpg" \
	"${tmpdir}/Packages.gz.asc" "${tmpdir}/Packages.gz"

# release.json is intentionally pretty-printed by stage-site.sh.  Accept
# leading/trailing JSON whitespace while keeping every published value exact.
grep -Eq '^[[:space:]]*"platform_slug":[[:space:]]*"tdvp-k230-r1",[[:space:]]*$' "${tmpdir}/release.json"
grep -Eq '^[[:space:]]*"platform_id":[[:space:]]*"tdvp-k230-br2025[.]02[.]1-glibc2[.]33-rv64-lp64d-k6[.]6[.]36-r1",[[:space:]]*$' "${tmpdir}/release.json"
grep -Eq '^[[:space:]]*"feed_release":[[:space:]]*"r3",[[:space:]]*$' "${tmpdir}/release.json"
grep -Eq '^[[:space:]]*"architecture":[[:space:]]*"riscv64",[[:space:]]*$' "${tmpdir}/release.json"
grep -Eq '^[[:space:]]*"index":[[:space:]]*"Packages[.]gz",[[:space:]]*$' "${tmpdir}/release.json"
grep -Eq '^[[:space:]]*"signature":[[:space:]]*"Packages[.]asc"[[:space:]]*$' "${tmpdir}/release.json"

# Every candidate must be installable only on this exact firmware ABI.  This
# blocks a generic RISC-V feed or an accidentally published package that would
# be accepted on a different rootfs.
awk -v expected="${ABI_VERSION}" '
	BEGIN { package = 0; valid = 0; invalid = 0 }
	/^Package: / { package = 1 }
	/^Architecture: riscv64$/ { arch = 1 }
	/^Depends: / {
		dependencies = $0
		sub(/^Depends: /, "", dependencies)
		# opkg accepts a comma-delimited Depends field with or without a
		# following space.  The composable feed generator emits the compact
		# canonical form, while historical releases used comma-space; accept
		# both before checking the exact ABI relation.
		dependency_count = split(dependencies, dependency, /,/)
		for (dependency_index = 1; dependency_index <= dependency_count; dependency_index++) {
			gsub(/^[[:space:]]+|[[:space:]]+$/, "", dependency[dependency_index])
			if (dependency[dependency_index] == "tdvp-platform-abi (= " expected ")") {
				abi = 1
			}
		}
	}
	/^$/ {
		if (package) {
			if (!arch || !abi) invalid = 1
			else valid++
		}
		package = arch = abi = 0
	}
	END {
		if (package) {
			if (!arch || !abi) invalid = 1
			else valid++
		}
		exit (valid && !invalid) ? 0 : 1
	}
' "${tmpdir}/Packages" || {
	printf '%s\n' 'TDVP feed release gate: package index contains no exact ABI-gated riscv64 package' >&2
	exit 1
}

# r3 is a composable userland release.  It must contain leaf applications and
# independent GTK, TLS, graphics, media, image, SDL, and mGBA providers;
# otherwise a valid signature could still conceal an application that borrows
# a general-purpose runtime from the base image or bundles it privately.
for required_package in \
	tdvp-gba sdl2 sdl2-ttf libmgba \
	tdvp-netsurf tdvp-mpv \
	libgtk-3-0 libcurl-4 libpng16-16 libjpeg-9 \
	glib-networking gtk3-data gdk-pixbuf-loaders pulse-modules; do
	grep -Eq "^Package: ${required_package}$" "${tmpdir}/Packages" || {
		printf 'TDVP feed release gate: required r3 package is missing: %s\n' \
			"${required_package}" >&2
		exit 1
	}
done

printf '%s\n' 'TDVP published opkg feed release gate: PASS'

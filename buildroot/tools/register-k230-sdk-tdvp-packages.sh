#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  register-k230-sdk-tdvp-packages.sh <sdk-worktree>

Register TDVP-owned Buildroot packages after the vendor SDK `sync` target has
copied the platform overlay. This script adds the SDK-local package entries to
the synchronised top-level `package/Config.in` through one idempotent step.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
BR_SRC="$WORKTREE/output/buildroot-2025.02.1"
PACKAGE_CONFIG="$BR_SRC/package/Config.in"
MANAGED_BEGIN='# TDVP managed package entries: begin'
MANAGED_END='# TDVP managed package entries: end'

declare -a PACKAGE_SOURCES=()

fail() {
	printf 'TDVP SDK package registration: %s\n' "$*" >&2
	exit 1
}

[ -f "$PACKAGE_CONFIG" ] || fail "run SDK make sync before registration: $PACKAGE_CONFIG"
register_package() {
	local name="$1"
	local needs_local_source="${2:-0}"
	local source_line="source \"package/$name/Config.in\""

	[ -f "$BR_SRC/package/$name/Config.in" ] || fail "missing $name Config.in after SDK sync"
	[ -f "$BR_SRC/package/$name/$name.mk" ] || fail "missing $name mk after SDK sync"
	if [ "$needs_local_source" = "1" ]; then
		[ -d "$BR_SRC/package/$name/src" ] || fail "missing $name source after SDK sync"
	fi

	PACKAGE_SOURCES+=("$source_line")
}

register_package tdvp-dejavu-fonts
# The vendor SDK carries labwc's recipe but omits it from its synchronized
# top-level package menu. Register it here so the profile's explicit Labwc
# selection reaches Kconfig.
register_package labwc
register_package swaylock 1
register_package gtk-layer-shell
register_package gtkmm3
register_package libfm-extra
register_package libmenu-cache
register_package libfm
register_package pcmanfm
register_package wf-panel-pi
register_package libnma
register_package nm-connection-editor
register_package wfplug-batt
register_package wfplug-menu
register_package wfplug-clock
register_package wfplug-netman
register_package wfplug-power
register_package wfplug-volumepulse
register_package wofi
register_package wvkbd
register_package tdvp-greetd
register_package tdvp-gtkgreet
register_package tdvp-greeter 1
register_package tdvp-opkg-trust 1
register_package tdvp-kpu-acceptance 1
register_package tdvp-labwc-desktop 1
register_package vicliu-pocket-linux-desktop 1
register_package vicliu-pocket-linux-hardware 1
register_package tdvp-display-smoke 1
register_package tdvp-keyboard-layout 1
register_package tdvp-wayland-acceptance 1

# The vendor SDK sync target may replace package/Config.in with the upstream
# Buildroot copy. Insert our own small, idempotent section immediately before
# the top-level menu closing marker instead of relying on a vendor-only anchor.
sed -i "/^${MANAGED_BEGIN}$/,/^${MANAGED_END}$/d" "$PACKAGE_CONFIG"
[ "$(tail -n 1 "$PACKAGE_CONFIG")" = 'endmenu' ] || \
	fail "unexpected package/Config.in footer; cannot install TDVP package menu"

TMP_CONFIG="$(mktemp "${PACKAGE_CONFIG}.XXXXXX")"
trap 'rm -f "$TMP_CONFIG"' EXIT
sed '$d' "$PACKAGE_CONFIG" > "$TMP_CONFIG"
{
	printf '%s\n' "$MANAGED_BEGIN"
	printf '%s\n' 'menu "TDVP platform packages"'
	for source_line in "${PACKAGE_SOURCES[@]}"; do
		printf '\t%s\n' "$source_line"
	done
	printf '%s\n' 'endmenu'
	printf '%s\n' "$MANAGED_END"
	printf '%s\n' 'endmenu'
} >> "$TMP_CONFIG"
mv "$TMP_CONFIG" "$PACKAGE_CONFIG"
trap - EXIT

for source_line in "${PACKAGE_SOURCES[@]}"; do
	grep -Fq "$source_line" "$PACKAGE_CONFIG" || fail "failed to register $source_line"
done
printf '%s\n' 'TDVP SDK package registration: desktop and hardware packages are registered'

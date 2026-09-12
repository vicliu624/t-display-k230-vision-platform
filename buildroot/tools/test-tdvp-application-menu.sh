#!/bin/sh
# Exercise the production XDG menu with the actual menu-cache generator.
# Accept an explicit menu source directory for running the same test on a board.
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
menu_source=${1:-$project/user-space/tdvp-labwc-desktop/src/menus}
generator=${TDVP_MENU_CACHE_GEN:-/usr/libexec/menu-cache/menu-cache-gen}
if [ -z "${TDVP_MENU_CACHE_GEN:-}" ] && [ ! -x "$generator" ]; then
    generator=/usr/lib/menu-cache/menu-cache-gen
fi
test -x "$generator" || { echo "menu-cache-gen required: $generator" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
export XDG_CONFIG_HOME="$work/config" XDG_CONFIG_DIRS="$work/config"
export XDG_DATA_HOME="$work/data" XDG_DATA_DIRS="$work/data"
export XDG_CACHE_HOME="$work/cache" LC_ALL=C CACHE_GEN_VERSION=1.2
unset XDG_MENU_PREFIX
mkdir -p "$work/config/menus" "$work/data/applications" "$work/data/desktop-directories" "$work/cache"
cp "$menu_source/lxde-applications.menu" "$work/config/menus/"
cp "$menu_source/"*.directory "$work/data/desktop-directories/"

fixture() {
    printf '[Desktop Entry]\nType=Application\nName=TDVP menu test %s\nExec=/bin/true\nTryExec=%s\nCategories=%s\n%s\n' \
        "$1" "${4:-/bin/true}" "$2" "${3:-}" > "$work/data/applications/$1.desktop"
}
generate() {
    "$generator" -i lxde-applications.menu -o "$work/result" -l C
}
expect_group() {
    awk -v id="-$1.desktop" -v expected="+$2" '
        /^\+/ {group=$0}
        $0==id {if (group!=expected) exit 1; found=1}
        END {if (!found) exit 1}
    ' "$work/result" || { echo "Wrong/missing category: $1 -> $2" >&2; exit 1; }
}

fixture browser 'Network;WebBrowser;'
fixture audio 'AudioVideo;Audio;Player;'
fixture graphics 'Graphics;'
fixture office 'Office;'
fixture development 'Development;'
fixture education 'Education;'
fixture science 'Science;'
fixture game 'Game;'
fixture utility 'Utility;'
fixture settings 'Settings;'
fixture system 'System;'
fixture unclassified ''
fixture future 'X-TDVP-FutureCategory;'
fixture missing 'Network;' '' '/tdvp-menu-test-command-that-does-not-exist'
fixture hidden 'Network;' 'Hidden=true'
fixture nodisplay 'Network;' 'NoDisplay=true'
generate
expect_group browser Internet
expect_group audio 'Sound & Video'
expect_group graphics Graphics
expect_group office Office
expect_group development Development
expect_group education Education
expect_group science Science
expect_group game Games
expect_group utility Accessories
expect_group settings Preferences
expect_group system System
expect_group unclassified Other
expect_group future Other
if grep -Eq '^-(hidden|nodisplay)\.desktop$' "$work/result"; then
    echo 'Hidden/NoDisplay entry leaked into the visible cache' >&2; exit 1
fi
# Cache v1.2 retains TryExec for libmenu-cache's runtime visibility check.
# The panel calls menu_cache_app_get_is_visible(), which checks executability.
grep -qx '/tdvp-menu-test-command-that-does-not-exist' "$work/result"
# Repeat using the same directories to exercise generation after removal.
rm "$work/data/applications/browser.desktop" "$work/data/applications/unclassified.desktop"
generate
if grep -Eq '^-(browser|unclassified)\.desktop$' "$work/result"; then
    echo 'Removed entry survived menu regeneration' >&2; exit 1
fi
expect_group future Other
echo 'TDVP application menu: PASS real generator, 13 categories/fallback cases, hidden entries, TryExec metadata and removal'

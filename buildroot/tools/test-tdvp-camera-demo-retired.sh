#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
package="$project/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT

test ! -e "$package/src/bin/vpl-camera"
test ! -e "$package/src/applications/vpl-camera.desktop"
cp -a "$package/src" "$temporary/build"
# Simulate files surviving an additive SDK/package source synchronization.
printf 'retired camera source\n' > "$temporary/build/bin/vpl-camera"
printf 'retired camera desktop source\n' > "$temporary/build/applications/vpl-camera.desktop"
mkdir -p "$temporary/target/usr/share/applications" "$temporary/target/usr/local/bin"
printf 'unrelated user application\n' > "$temporary/target/usr/share/applications/keep.desktop"
printf 'include %s\n.PHONY: %s/build/install\n%s/build/install:\n\t$(VICLIU_POCKET_LINUX_DESKTOP_INSTALL_TARGET_CMDS)\n' \
    "$package/vicliu-pocket-linux-desktop.mk" "$temporary" "$temporary" > "$temporary/Makefile"

for state in fresh reused; do
    if [ "$state" = reused ]; then
        cp "$temporary/build/bin/vpl-camera" "$temporary/target/usr/local/bin/vpl-camera"
        cp "$temporary/build/applications/vpl-camera.desktop" "$temporary/target/usr/share/applications/vpl-camera.desktop"
    fi
    # Execute the actual production recipe, including every install command.
    make --no-print-directory -f "$temporary/Makefile" \
        TARGET_DIR="$temporary/target" INSTALL=install "$temporary/build/install" >/dev/null
    test ! -e "$temporary/target/usr/local/bin/vpl-camera"
    test ! -L "$temporary/target/usr/local/bin/vpl-camera"
    test ! -e "$temporary/target/usr/share/applications/vpl-camera.desktop"
    test ! -L "$temporary/target/usr/share/applications/vpl-camera.desktop"
    grep -Fxq 'unrelated user application' "$temporary/target/usr/share/applications/keep.desktop"
    cmp "$package/src/applications/vpl-display.desktop" "$temporary/target/usr/share/applications/vpl-display.desktop"
    cmp "$package/src/bin/vpl-logs" "$temporary/target/usr/local/bin/vpl-logs"
done

python3 - "$project" <<'PY'
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

project = Path(sys.argv[1])
menu = ET.parse(project / 'user-space/tdvp-labwc-desktop/src/menus/lxde-applications.menu')
assert 'vpl-camera.desktop' not in [entry.text for entry in menu.iter('Filename')]
assert {'Accessories', 'Games', 'Preferences', 'System'} <= {
    entry.text for entry in menu.iter('Menuname')}
guard = (project / 'buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh').read_text()
for path in ('/usr/local/bin/vpl-camera', '/usr/share/applications/vpl-camera.desktop'):
    assert 'reject_fs_path "${ROOTFS}" ' + repr(path) in guard
    assert 'require_fs_path "${ROOTFS}" ' + repr(path) not in guard
assert "reject_rootfs_content '/etc/xdg/menus/lxde-applications.menu' 'vpl-camera.desktop'" in guard
PY
echo 'TDVP camera demo retirement: PASS real fresh/reused installs, stale sources ignored, unrelated apps preserved, menu and image rejection contract'

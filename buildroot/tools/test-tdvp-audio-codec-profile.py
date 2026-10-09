#!/usr/bin/env python3
"""Guard the Buildroot external-codec selection required by audio consumers."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
profile = root / "buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
settings = {}
for line in profile.read_text().splitlines():
    if line.startswith("BR2_") and "=" in line:
        key, value = line.split("=", 1)
        assert key not in settings, "duplicate profile setting: " + key
        settings[key] = value
for name in ("PULSEAUDIO", "FLAC", "LIBVORBIS", "OPUS"):
    assert settings.get("BR2_PACKAGE_" + name) == "y", "missing audio prerequisite: " + name
builder = root / "buildroot/tools/build-k230-sdk-rm69a10.sh"
source = builder.read_text()
match = re.search(r"^\s*product_packages=\(\n(.*?)^\s*\)", source, re.MULTILINE | re.DOTALL)
assert match, "missing product package invalidation list"
packages = [line.strip() for line in match.group(1).splitlines() if line.strip() and not line.strip().startswith("#")]
assert packages.count("libsndfile") == 1, "libsndfile external-codec changes must invalidate the old package"
# Execute the actual shell array, then the same full-build package selection.
fixture = match.group(0) + '\npackages_to_clean=("${product_packages[@]}")\nprintf "%s\\n" "${packages_to_clean[@]}"\n'
selected = subprocess.check_output(["bash", "-c", fixture], text=True).splitlines()
assert selected.count("libsndfile") == 1
print("TDVP libsndfile external-codec profile and incremental rebuild selection: PASS")

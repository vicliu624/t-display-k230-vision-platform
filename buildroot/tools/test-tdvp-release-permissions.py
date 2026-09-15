#!/usr/bin/env python3
"""Exercise the production collector across producer/consumer Unix identities.

Run as root (sudo or a disposable container) so the collector can run as UID
1001 and its public files can be read as UID 1000. Heavy image/SDK builders are
fixture stand-ins here; the production SDK relocation test covers real builds.
"""

import gzip
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
PROFILE = "k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def consumer(bundle):
    return subprocess.run(["sha256sum", "--strict", "-c", "SHA256SUMS"], cwd=bundle,
                          user=1000, group=1000, extra_groups=(),
                          capture_output=True, text=True, timeout=30)


def check(root, mask):
    project = root / "project"
    scripts = project / "buildroot/tools"
    scripts.mkdir(parents=True)
    shutil.copy2(HERE / "collect-release-bundle.sh", scripts)
    write(scripts / "assert-public-release.sh", "#!/bin/sh\nexit 0\n")
    write(project / "buildroot/k230-sdk-overlay/board/tdvp/verify-opkg-rootfs.sh", '''#!/bin/sh
set -eu
printf '{}\\n' > "$2/tdvp-image-base.json"
printf 'fixture status\\n' > "$2/tdvp-opkg-status"
printf 'fixture info\\n' | gzip -n > "$2/tdvp-opkg-info.tar.gz"
''')
    write(scripts / "export-tdvp-sdk.py", '''import gzip, json, sys
from pathlib import Path
bundle = Path(sys.argv[2])
name = sys.argv[3] + "-cpu0-sdk.tar.gz"
(bundle / name).write_bytes(gzip.compress(b"fixture SDK", mtime=0))
(bundle / "tdvp-sdk-manifest.json").write_text(json.dumps({"archive": name}))
''')
    sdk = root / "sdk"
    images = sdk / "output" / PROFILE / "images"
    images.mkdir(parents=True)
    # post-image.sh compresses into mktemp then renames it; preserve its 0600.
    with tempfile.NamedTemporaryFile(dir=images, delete=False) as stream:
        stream.write(gzip.compress(b"fixture SD image", mtime=0))
        compressed = Path(stream.name)
    image = images / "sysimage-sdcard.img.gz"
    compressed.rename(image)
    for name in ("tdvp-image-manifest", "tdvp-cpu1-rtsmart.bin", "tdvp-cpu1-rtsmart.manifest"):
        write(images / name, name + "\n")
    write(sdk / ".tdvp/sdk-baseline-manifest", "fixture baseline\n")
    write(sdk / "output" / PROFILE / "build/tdvp-package-info.json", "{}\n")
    sources = [image, *images.glob("tdvp-*"), sdk / ".tdvp/sdk-baseline-manifest",
               sdk / "output" / PROFILE / "build/tdvp-package-info.json"]
    for path in sources:
        path.chmod(0o600)
    original = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    # Only this disposable tree changes owner. No system accounts are needed.
    for directory, _, files in os.walk(root):
        os.chown(directory, 1001, 1001)
        Path(directory).chmod(0o755)
        for name in files:
            os.chown(Path(directory) / name, 1001, 1001)
    result = subprocess.run(["bash", scripts / "collect-release-bundle.sh", sdk, "candidate"],
                            user=1001, group=1001, extra_groups=(), umask=mask,
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    bundle = project / "output/candidate"
    result = consumer(bundle)
    if result.returncode:
        raise AssertionError("UID 1000 cannot verify UID 1001's release (umask {:03o}):\n{}{}".format(
            mask, result.stdout, result.stderr))
    assert bundle.stat().st_mode & 0o7777 == 0o755, "release directory must be 0755"
    for path in bundle.iterdir():
        assert path.is_file() and path.stat().st_mode & 0o7777 == 0o644, str(path)
        assert path.stat().st_uid == 1001, str(path)
    for path, digest in original.items():
        assert path.stat().st_mode & 0o7777 == 0o600, "source permission changed: " + str(path)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, "source changed: " + str(path)
    (bundle / "candidate.img.gz").chmod(0o600)
    denied = consumer(bundle)
    assert denied.returncode != 0 and "Permission denied" in denied.stderr, "consumer accidentally privileged"
    print("Release permissions: PASS producer=1001 consumer=1000 umask={:03o}; all assets readable, sources intact, 0600 regression rejected".format(mask))


def main():
    if os.geteuid() != 0:
        sys.exit("Run with sudo or in a disposable root container to test distinct UIDs")
    for mask in (0o022, 0o077):
        with tempfile.TemporaryDirectory(prefix="tdvp-release-permissions-") as directory:
            check(Path(directory), mask)


if __name__ == "__main__":
    main()

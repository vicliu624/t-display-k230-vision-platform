#!/usr/bin/env python3
"""Export the CPU0 application SDK from one completed image build.

This reads the build tree and writes only a new SDK/archive. It never invokes
Buildroot's world/prepare-sdk targets or changes an existing image.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import tempfile


PROFILE = "k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
TRIPLE = "riscv64-unknown-linux-gnu"
ARCH = "rv64imafdc_zicsr_zifencei"
TOOLCHAIN = "Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V3.0.2"
HERE = Path(__file__).resolve().parent
BINDINGS = ("tdvp-image-manifest", "tdvp-sdk-baseline-manifest", "tdvp-image-base.json",
            "tdvp-opkg-status", "tdvp-opkg-info.tar.gz", "tdvp-buildroot-packages.json")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def configuration(path):
    values = {}
    for line in path.read_text().splitlines():
        if line.startswith("BR2_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = json.loads(value) if value.startswith('"') else value
    required = {"BR2_riscv": "y", "BR2_RISCV_64": "y", "BR2_RISCV_ISA_RVC": "y",
                "BR2_GCC_TARGET_ABI": "lp64d", "BR2_TOOLCHAIN_EXTERNAL_PREINSTALLED": "y",
                "BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX": TRIPLE,
                "BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC": "y", "BR2_TOOLCHAIN_EXTERNAL_GCC_14": "y",
                "BR2_PIC_PIE": "y", "BR2_SSP_STRONG": "y", "BR2_RELRO_FULL": "y",
                "BR2_FORTIFY_SOURCE_1": "y", "BR2_TARGET_OPTIMIZATION": "-mcpu=c908 -mtune=c908"}
    required.update({"BR2_RISCV_ISA_RV" + extension: "y" for extension in "IMAFD"})
    for key, expected in required.items():
        if values.get(key) != expected:
            raise ValueError("unsupported SDK policy: {} expected={!r} actual={!r}".format(key, expected, values.get(key)))
    if values.get("BR2_RISCV_ISA_RVV") == "y":
        raise ValueError("CPU0 SDK cannot enable RVV")
    return values


def inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def relocate_links(tree, old_root, sdk_root):
    """Keep sysroot absolute links inside the sysroot; reject other escapes."""
    for directory, dirs, files in os.walk(tree, followlinks=False):
        for name in dirs + files:
            link = Path(directory) / name
            if not link.is_symlink():
                continue
            target = os.readlink(link)
            if target.startswith("/"):
                old = Path(target)
                if inside(old, old_root):
                    destination = tree / old.relative_to(old_root)
                elif tree.name == "sysroot":
                    destination = tree / target.lstrip("/")
                else:
                    raise ValueError("external toolchain symlink: " + str(link))
                link.unlink()
                link.symlink_to(os.path.relpath(destination, link.parent))
            if not inside(link.resolve(), sdk_root):
                raise ValueError("SDK symlink escapes archive: " + str(link))


def relocate_development_files(sysroot, old_staging):
    # pkg-config applies PKG_CONFIG_SYSROOT_DIR to target-root-relative paths.
    # CMake imports resolve relative to their own file; libtool metadata keeps
    # target-root-relative paths. Never leave a build-host path in these files.
    for directory, _, files in os.walk(sysroot):
        for name in files:
            path = Path(directory) / name
            if path.is_symlink() or path.suffix not in (".pc", ".la", ".cmake"):
                continue
            content = path.read_text()
            replacement = ""
            if path.suffix == ".cmake":
                replacement = "${CMAKE_CURRENT_LIST_DIR}/" + os.path.relpath(sysroot, path.parent)
            updated = content.replace(str(old_staging), replacement)
            if updated != content:
                path.write_text(updated)


def image_libraries(sysroot, image, inventory):
    """Use final-image shared-library bytes, keeping staging headers/linker files."""
    requests = []
    for name, record in sorted(inventory["files"].items()):
        if record["type"] != "file" or not name.startswith("/usr/lib/") or not re.search(r"\.so(?:\.|$)", name):
            continue
        destination = sysroot / name.lstrip("/")
        if destination.is_file() and not destination.is_symlink():
            if any(c in name + str(destination) for c in '\n\r"\\'):
                raise ValueError("unsupported debugfs filename")
            requests.append((name, record, destination))
    if not requests:
        raise ValueError("no final-image libraries found in SDK staging")
    commands = "".join('dump "{}" "{}"\n'.format(name, destination) for name, _, destination in requests)
    result = run(["debugfs", "-f", "-", image], input=commands.encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for name, record, destination in requests:
        if sha256(destination) != record["sha256"]:
            raise ValueError("SDK/image library differs: " + name + "\n" + result.stderr.decode(errors="replace")[-1000:])
        destination.chmod(record["mode"])
    return {name: record["sha256"] for name, record, _ in requests}


def copy_toolchain(source, destination):
    # The vendor sysroot contains several ABIs. The paired Buildroot sysroot
    # supplies the selected ABI and the actual image's development packages.
    for entry in sorted(source.iterdir()):
        if entry.name == "sysroot":
            continue
        if entry.is_dir() and not entry.is_symlink():
            shutil.copytree(entry, destination / entry.name, symlinks=True)
        else:
            shutil.copy2(entry, destination / entry.name, follow_symlinks=False)
    (destination / "sysroot").symlink_to("../sysroot")


def build_entry_points(root, buildroot, config):
    source = buildroot / "toolchain/toolchain-wrapper.c"
    license_dir = root / "share/licenses/buildroot-wrapper"
    license_dir.mkdir(parents=True)
    shutil.copy2(source, license_dir / source.name)
    shutil.copy2(buildroot / "COPYING", license_dir / "COPYING")
    # Add the scalar ISA to the unconditional defaults as well. Buildroot's
    # BR_ARCH alone is skipped when a caller supplies only -mtune/-mcpu.
    flags = shlex.split(config["BR2_TARGET_OPTIMIZATION"])
    flags += ["-march=" + ARCH, "-fstack-protector-strong", "-Wl,--build-id=none"]
    arguments = ['-DBR_SYSROOT="sysroot"', '-DBR_CROSS_PATH_REL="toolchain/bin"',
                 '-DBR_CROSS_PATH_SUFFIX=""', '-DBR_ARCH="' + ARCH + '"', '-DBR_ABI="lp64d"',
                 "-DBR2_PIC_PIE", "-DBR2_RELRO_FULL",
                 "-DBR_ADDITIONAL_CFLAGS=" + ",".join(json.dumps(flag) for flag in flags) + ","]
    run(["cc", "-O2", "-s", "-Wl,--build-id=none"] + arguments + [source, "-o", root / "bin/toolchain-wrapper"])
    for suffix in ("gcc", "g++", "cc", "c++", "cpp"):
        real = root / "toolchain/bin" / (TRIPLE + "-" + suffix)
        if not real.exists() and suffix in ("cc", "c++"):
            real.symlink_to(TRIPLE + ("-gcc" if suffix == "cc" else "-g++"))
        if not real.exists():
            raise ValueError("missing compiler: " + suffix)
        (root / "bin" / real.name).symlink_to("toolchain-wrapper")
    for suffix in ("ar", "as", "ld", "nm", "objcopy", "objdump", "ranlib", "readelf", "size", "strings", "strip",
                   "gcc-ar", "gcc-nm", "gcc-ranlib"):
        name = TRIPLE + "-" + suffix
        if not (root / "toolchain/bin" / name).exists():
            raise ValueError("missing binary utility: " + name)
        (root / "bin" / name).symlink_to("../toolchain/bin/" + name)


def tree_inventory(root):
    records = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            path = Path(directory) / name
            relative = path.relative_to(root).as_posix()
            if relative == "tdvp-sdk-manifest.json":
                continue
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                if not inside(path.resolve(), root):
                    raise ValueError("invalid SDK link: " + relative)
                records[relative] = {"type": "symlink", "target": os.readlink(path)}
            elif stat.S_ISREG(mode):
                records[relative] = {"type": "file", "sha256": sha256(path), "mode": stat.S_IMODE(mode)}
            elif not stat.S_ISDIR(mode):
                raise ValueError("special file in SDK: " + relative)
    return records


def development_permissions(root):
    # A development archive is readable by its consuming UID. Do not carry
    # target account restrictions or setuid/setgid capabilities onto a host.
    for directory, dirs, files in os.walk(root, followlinks=False):
        Path(directory).chmod(0o755)
        for name in files:
            path = Path(directory) / name
            if not path.is_symlink():
                path.chmod(0o755 if path.stat().st_mode & 0o111 else 0o644)


def export(worktree, bundle, release):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", release):
        raise ValueError("invalid release name")
    archive = bundle / (release + "-cpu0-sdk.tar.gz")
    manifest_path = bundle / "tdvp-sdk-manifest.json"
    if archive.exists() or archive.is_symlink() or manifest_path.exists() or manifest_path.is_symlink():
        raise ValueError("SDK output already exists; choose a fresh release bundle")
    output = worktree / "output" / PROFILE
    config = configuration(output / ".config")
    toolchain = Path(config["BR2_TOOLCHAIN_EXTERNAL_PATH"]).resolve()
    if toolchain.name != TOOLCHAIN:
        raise ValueError("unexpected external toolchain")
    staging = (output / "host/riscv64-buildroot-linux-gnu/sysroot").resolve()
    image = output / "images/rootfs.ext2"
    image_manifest = dict(line.split("=", 1) for line in (bundle / "tdvp-image-manifest").read_text().splitlines() if "=" in line)
    image_file = bundle / (release + ".img.gz")
    for path, key in ((image, "rootfs.ext2_sha256"), (image_file, "sysimage-sdcard.img.gz_sha256")):
        if sha256(path) != image_manifest.get(key):
            raise ValueError("image provenance mismatch: " + key)
    bindings = {name: sha256(bundle / name) for name in BINDINGS}
    bindings[image_file.name] = sha256(image_file)
    inventory = json.loads((bundle / "tdvp-image-base.json").read_text())
    if inventory.get("schema") != 1 or inventory.get("ownership_mode") != "buildroot":
        raise ValueError("SDK requires the final Buildroot-owned image inventory")
    with tempfile.TemporaryDirectory(prefix=".tdvp-sdk-", dir=bundle.parent) as temporary:
        root = Path(temporary) / "tdvp-sdk"
        root.mkdir()
        (root / "bin").mkdir()
        (root / "toolchain").mkdir()
        shutil.copytree(staging, root / "sysroot", symlinks=True)
        copy_toolchain(toolchain, root / "toolchain")
        relocate_links(root / "sysroot", staging, root)
        relocate_links(root / "toolchain", toolchain, root)
        relocate_development_files(root / "sysroot", staging)
        libraries = image_libraries(root / "sysroot", image, inventory)
        build_entry_points(root, worktree / "output/buildroot-2025.02.1", config)
        shutil.copy2(output / "host/bin/pkgconf", root / "bin/pkgconf")
        (root / "lib").mkdir()
        pkgconf_libraries = list((output / "host/lib").glob("libpkgconf.so*"))
        if not pkgconf_libraries:
            raise ValueError("missing host pkgconf library")
        for library in pkgconf_libraries:
            shutil.copy2(library, root / "lib" / library.name, follow_symlinks=False)
        templates = HERE / "sdk"
        for entry in templates.iterdir():
            if entry.is_file() and entry.name != "Dockerfile.validation":
                shutil.copy2(entry, root / entry.name)
                (root / entry.name).chmod(0o755 if entry.suffix in (".sh", ".py") else 0o644)
        (root / "bin/pkg-config").symlink_to("../pkg-config.sh")
        (root / "metadata").mkdir()
        for name in BINDINGS:
            shutil.copy2(bundle / name, root / "metadata" / name)
        development_permissions(root)
        compiler_version = subprocess.check_output([str(root / "bin" / (TRIPLE + "-gcc")), "-dumpfullversion"], text=True).strip()
        records = tree_inventory(root)
        manifest = {"schema": 1, "kind": "tdvp-cpu0-application-sdk", "release_name": release,
                    "archive": archive.name, "host": "x86_64-linux-gnu", "validated_host": "Ubuntu 24.04",
                    "target": TRIPLE, "march": ARCH, "mabi": "lp64d", "compiler_version": compiler_version,
                    "toolchain": TOOLCHAIN, "buildroot_config_sha256": sha256(output / ".config"),
                    "exporter_sha256": sha256(Path(__file__)), "image_bindings": bindings,
                    "image_library_sha256": libraries, "files": records,
                    "files_sha256": hashlib.sha256(json.dumps(records, sort_keys=True, separators=(",", ":")).encode()).hexdigest()}
        (root / "tdvp-sdk-manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n")
        # Verify before packing. A second, isolated relocation test runs in CI.
        run(["python3", root / "verify-sdk.py", root])
        packed = Path(temporary) / archive.name
        with packed.open("xb") as destination:
            tar = subprocess.Popen(["tar", "--sort=name", "--mtime=@0", "--owner=0", "--group=0", "--numeric-owner",
                                    "-C", str(root.parent), "-cf", "-", root.name], stdout=subprocess.PIPE)
            try:
                run(["gzip", "-n", "-6"], stdin=tar.stdout, stdout=destination)
            finally:
                tar.stdout.close()
            if tar.wait() != 0:
                raise ValueError("SDK archive failed")
        # Publish only a complete archive; link refuses to overwrite an output
        # which appeared during export. The temporary directory is on the same
        # filesystem as the release bundle.
        os.link(packed, archive)
        with manifest_path.open("x") as destination:
            destination.write((root / "tdvp-sdk-manifest.json").read_text())
    print("TDVP CPU0 SDK: PASS {} final-image libraries; {}".format(len(libraries), archive))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worktree", type=Path)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("release_name")
    args = parser.parse_args()
    try:
        export(args.worktree.resolve(), args.bundle.resolve(), args.release_name)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, "TDVP SDK export failed: {}\n".format(error))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Record real image ownership; optionally seed byte-identical runtime IPKs.

Run on a disposable Buildroot target at the end of post-fakeroot, never on the
live device. A catalog is build input, not an instruction to install its data
or execute maintainer scripts. Missing/different files reject the entire seed.
"""

import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tarfile


BASE = "tdvp-image-base"
ABI = "tdvp-platform-abi"
ABI_VERSION = "2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1"
MANIFEST = "/usr/share/tdvp/opkg/image-base.json"
NAME = re.compile(r"[a-z0-9][a-z0-9+.-]*\Z")


def inventory(root):
    records = {}
    for prefix in ("usr", "etc", "boot", "bin", "sbin", "lib", "lib64"):
        start = root / prefix
        paths = [start] if start.is_symlink() else []
        if start.is_dir() and not start.is_symlink():
            for directory, directories, files in os.walk(str(start), followlinks=False):
                paths.extend(Path(directory) / name for name in files)
                paths.extend(Path(directory) / name for name in directories
                             if (Path(directory) / name).is_symlink())
        for path in paths:
            relative = "/" + path.relative_to(root).as_posix()
            if relative == MANIFEST:
                continue
            if "\n" in relative or "\r" in relative:
                raise ValueError("unsupported image filename: " + repr(relative))
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                records[relative] = {"type": "symlink", "target": os.readlink(str(path)), "mode": stat.S_IMODE(mode)}
            elif stat.S_ISREG(mode):
                records[relative] = {"type": "file", "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                     "mode": stat.S_IMODE(mode)}
            else:
                raise ValueError("unexpected special file in image-owned tree: " + relative)
    if "/usr/bin/opkg" not in records or "/etc/opkg/opkg.conf" not in records:
        raise ValueError("target is missing the image opkg installation")
    return records


def control_fields(text):
    fields = {}
    for line in text.splitlines():
        if not line:
            continue
        key, separator, value = line.partition(": ")
        if not separator or key in fields or line[0].isspace():
            raise ValueError("unsupported/duplicate package control field: " + line)
        fields[key] = value
    package = fields.get("Package", "")
    version = fields.get("Version", "")
    if not NAME.fullmatch(package) or not re.fullmatch(r"[A-Za-z0-9.+:~_-]+", version):
        raise ValueError("invalid package identity")
    if package in (BASE, ABI) or fields.get("Architecture") != "riscv64":
        raise ValueError("invalid runtime package architecture/name: " + package)
    if any(key in fields for key in ("Replaces", "Conflicts", "Provides", "Conffiles")):
        raise ValueError("runtime ownership seed cannot use replacement/virtual ownership rules")
    return fields


def load_catalog(catalog, root, records):
    packages, owners = {}, {}
    ipks = sorted(catalog.glob("*.ipk"))
    if not ipks:
        raise ValueError("runtime seed catalog is empty: " + str(catalog))
    for ipk in ipks:
        control = subprocess.check_output(["ar", "p", str(ipk), "control.tar.gz"])
        with tarfile.open(fileobj=io.BytesIO(control), mode="r:gz") as archive:
            members = [member for member in archive if not member.isdir()]
            if len(members) != 1 or str(PurePosixPath(members[0].name)) != "control" or not members[0].isfile():
                raise ValueError("runtime seed requires control-only metadata: " + ipk.name)
            fields = control_fields(archive.extractfile(members[0]).read().decode("utf-8"))
        package = fields["Package"]
        if package in packages:
            raise ValueError("duplicate runtime package: " + package)
        package_paths = []
        data = subprocess.check_output(["ar", "p", str(ipk), "data.tar.gz"])
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            seen = set()
            for member in archive:
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts or "\n" in member.name or "\r" in member.name:
                    raise ValueError("unsafe runtime seed path: " + member.name)
                relative = "/" + str(path)
                if relative in seen:
                    raise ValueError("duplicate runtime seed path: " + relative)
                seen.add(relative)
                if member.isdir():
                    continue
                if relative not in records:
                    raise ValueError("runtime seed path absent from image: " + relative)
                if relative in owners:
                    raise ValueError("duplicate image ownership: " + relative)
                if member.isfile():
                    expected = {"type": "file", "sha256": hashlib.sha256(archive.extractfile(member).read()).hexdigest(),
                                "mode": member.mode & 0o7777}
                    if (root / relative.lstrip("/")).lstat().st_mode & 0o7777 != member.mode & 0o7777:
                        raise ValueError("runtime seed file mode differs: " + relative)
                elif member.issym():
                    expected = {"type": "symlink", "target": member.linkname, "mode": 0o777}
                else:
                    raise ValueError("unsupported runtime seed file type: " + relative)
                if records[relative] != expected:
                    raise ValueError("runtime seed payload differs from image: " + relative)
                owners[relative] = package
                package_paths.append(relative)
        if not package_paths:
            raise ValueError("runtime seed has no real image payload: " + package)
        packages[package] = (fields, sorted(package_paths))
    versions = {name: fields["Version"] for name, (fields, _) in packages.items()}
    versions[ABI] = ABI_VERSION
    for fields, _ in packages.values():
        for dependency in fields.get("Depends", "").split(","):
            match = re.fullmatch(r"\s*([a-z0-9][a-z0-9+.-]*)\s*(?:\(=\s*([^\s)]+)\))?\s*", dependency)
            if not match or match[1] not in versions or (match[2] and versions[match[1]] != match[2]):
                raise ValueError("unsatisfied installed runtime dependency: " + fields["Package"] + ": " + dependency)
    return packages, owners


def aliased_paths(records, paths):
    # opkg may receive /lib/foo or /usr/lib/foo on a merged-/usr image. Reserve
    # both names for the same owner; never follow an image symlink on the host.
    result = set(paths)
    for alias, record in records.items():
        if alias.count("/") != 1 or record["type"] != "symlink":
            continue
        target = record["target"]
        if target.startswith("usr/"):
            target = "/" + target
        if target not in ("/usr/bin", "/usr/sbin", "/usr/lib", "/usr/lib64"):
            continue
        for path in paths:
            if path.startswith(target + "/"):
                result.add(alias + path[len(target):])
    return sorted(result)


def buildroot_packages(build_info, build_dir, records, base_version):
    """Use selected Buildroot metadata and its installed-file accounting.

    Names are explicitly image-owned. Versions combine the upstream version
    with the final payload digest, so changed overlay/stripping bytes cannot
    impersonate an old public runtime IPK of the same upstream version.
    """
    selected = json.loads(build_info.read_text())
    claims, source_metadata, normalized_names = {}, {}, set()
    for name, metadata in sorted(selected.items()):
        if metadata.get("type") != "target" or metadata.get("virtual") or not metadata.get("install_target"):
            continue
        if not NAME.fullmatch(name.replace("_", "-")):
            raise ValueError("invalid Buildroot package name: " + name)
        if name.replace("_", "-") in normalized_names:
            raise ValueError("colliding Buildroot package name: " + name)
        normalized_names.add(name.replace("_", "-"))
        stamp_dir = PurePosixPath(metadata["stamp_dir"])
        if stamp_dir.is_absolute() or ".." in stamp_dir.parts or len(stamp_dir.parts) < 2 or stamp_dir.parts[0] != "build":
            raise ValueError("unsafe Buildroot stamp directory: " + str(stamp_dir))
        file_list = build_dir.joinpath(*stamp_dir.parts[1:]) / ".files-list.txt"
        if not file_list.is_file():
            raise ValueError("selected target package has no installed-file account: " + name)
        with file_list.open(newline="") as stream:
            for row in csv.reader(stream):
                if len(row) != 2 or row[0] != name:
                    raise ValueError("invalid Buildroot file account: " + str(file_list))
                path = PurePosixPath(row[1])
                if path.is_absolute() or ".." in path.parts or "\n" in row[1] or "\r" in row[1]:
                    raise ValueError("unsafe Buildroot file path: " + row[1])
                relative = "/" + str(path)
                # Account for merged-/usr file names without resolving any
                # target absolute symlink against the host filesystem.
                for alias in ("lib", "lib64", "bin", "sbin"):
                    link = records.get("/" + alias, {})
                    if relative.startswith("/" + alias + "/") and link.get("type") == "symlink":
                        target = link["target"].lstrip("/")
                        if target == "usr/" + alias:
                            relative = "/usr" + relative
                            break
                if relative in records:
                    claims.setdefault(relative, set()).add(name)
        source_metadata[name] = metadata

    paths_by_package, owners = {}, {}
    for path, names in sorted(claims.items()):
        # Overlays and two packages can touch one path. Keep an ambiguous path
        # under the image base owner and record the ambiguity for maintainers.
        if len(names) != 1:
            continue
        name = next(iter(names))
        package = "tdvp-image-" + name.replace("_", "-")
        paths_by_package.setdefault(package, []).append(path)
        owners[path] = package
    packages, sources, versions = {}, {}, {BASE: base_version, ABI: ABI_VERSION}
    for name, metadata in sorted(source_metadata.items()):
        package = "tdvp-image-" + name.replace("_", "-")
        paths = paths_by_package.get(package, [])
        if not paths:
            continue
        source_version = metadata.get("version") or "0"
        if not re.fullmatch(r"[A-Za-z0-9.+:~_-]+", source_version):
            raise ValueError("invalid Buildroot source version: " + source_version)
        digest = hashlib.sha256(json.dumps({path: records[path] for path in paths},
                                          sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        version = source_version + "+tdvp." + digest
        versions[package] = version
        packages[package] = ({"Package": package, "Version": version, "Architecture": "riscv64",
                              "Description": "Image-owned Buildroot " + name}, paths)
        sources[package] = {"buildroot_name": name, "source_version": source_version,
                            "licenses": metadata.get("licenses", "unknown")}
    if not packages:
        raise ValueError("selected Buildroot metadata did not account for any image files")

    def installed_dependencies(name, visited):
        result = set()
        for dependency in selected.get(name, {}).get("dependencies", []):
            if dependency in visited or dependency.startswith("host-"):
                continue
            visited.add(dependency)
            package = "tdvp-image-" + dependency.replace("_", "-")
            if package in packages:
                result.add(package)
            else:
                result.update(installed_dependencies(dependency, visited))
        return result

    for package, (fields, _) in packages.items():
        dependencies = installed_dependencies(sources[package]["buildroot_name"], set()) - {package}
        dependencies.update((BASE, ABI))
        fields["Depends"] = ", ".join("{} (= {})".format(name, versions[name]) for name in sorted(dependencies))
    ambiguities = {path: sorted(names) for path, names in claims.items() if len(names) > 1}
    return packages, owners, sources, ambiguities


def seed(root, catalog=None, build_info=None, build_dir=None):
    root = root.resolve()
    if root == Path("/"):
        raise ValueError("refusing to seed a live root filesystem")
    status_path = root / "var/lib/opkg/status"
    status = status_path.read_text()
    if "Package: " + ABI + "\n" not in status or "Version: " + ABI_VERSION + "\n" not in status:
        raise ValueError("target has no matching Buildroot ABI seed")
    existing = set(re.findall(r"^Package: (.+)$", status, re.M))
    # post-build creates the ABI-only database before invoking this helper.
    # Do not discard an operator's installed-package database by accident.
    if existing - {ABI, BASE}:
        raise ValueError("target already has runtime/application package state; regenerate through post-build")
    records = inventory(root)
    digest = hashlib.sha256(json.dumps(records, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    sources, ambiguities = {}, {}
    if build_info:
        if catalog or not build_dir:
            raise ValueError("Buildroot seeding requires build-dir and cannot also use a catalog")
        packages, owners, sources, ambiguities = buildroot_packages(build_info, build_dir, records, "1+" + digest)
    else:
        packages, owners = load_catalog(catalog, root, records) if catalog else ({}, {})
    packages[BASE] = ({"Package": BASE, "Version": "1+" + digest,
                       "Architecture": "riscv64", "Description": "Image-owned boot and desktop files"},
                      sorted((set(records) - set(owners)) | {MANIFEST}))
    packages[ABI] = ({"Package": ABI, "Version": ABI_VERSION, "Architecture": "riscv64",
                      "Description": "TDVP K230 firmware ABI identity"}, [])
    package_status = []
    info = root / "var/lib/opkg/info"
    info.mkdir(parents=True, exist_ok=True)
    for name, (fields, paths) in sorted(packages.items()):
        # All validations precede writes. A seed never extracts payloads or
        # runs postinst, and each installed version comes from verified bytes.
        # opkg serializes Status as want / flags / state. HOLD is a flag;
        # putting it in the first column logs an error but can still exit 0.
        fields = dict(fields, Essential="yes", Status="install hold installed")
        record = "".join("{}: {}\n".format(key, value) for key, value in fields.items())
        package_status.append(record)
        (info / (name + ".list")).write_text("".join(path + "\n" for path in aliased_paths(records, paths)))
        (info / (name + ".control")).write_text(record)
    status_path.write_text("\n".join(package_status) + "\n")
    manifest = root / MANIFEST.lstrip("/")
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps({"schema": 1, "package": BASE, "version": "1+" + digest,
                                    "ownership_mode": "buildroot" if build_info else "catalog" if catalog else "base-only",
                                    "package_sources": sources, "ambiguous_buildroot_paths": ambiguities,
                                    "owners": {path: owners.get(path, BASE) for path in records},
                                    "installed_packages": {name: fields for name, (fields, _) in packages.items()},
                                    "runtime_packages": sorted(set(packages) - {ABI, BASE}),
                                    "files": records}, sort_keys=True, indent=2) + "\n")
    print("TDVP opkg image seed: {} owned paths, {} verified runtime packages".format(len(records), len(packages) - 2))


def ext4_file_modes(image, records):
    """Read inode permissions in one read-only debugfs batch.

    rdump preserves payload bytes but drops setuid/setgid on extracted files.
    Neither those host modes nor the expected manifest can supply the actual
    image permissions. Query every observed path, including ordinary files so
    an unexpected newly added privilege bit is also detected.
    """
    paths = sorted(records)
    for path in paths:
        if not path.startswith("/") or any(character in path for character in '\n\r"\\'):
            raise ValueError("unsupported debugfs path: " + repr(path))
    commands = "".join('stat "{}"\n'.format(path) for path in paths)
    result = subprocess.run(["debugfs", "-f", "-", str(image)], input=commands.encode(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=dict(os.environ, LC_ALL="C"))
    output = result.stdout.decode("utf-8", errors="replace")
    entries = re.findall(r"^Inode:\s+\d+\s+Type:\s+(.*?)\s+Mode:\s+([0-7]+)\s+Flags:", output, re.M)
    # debugfs may return success after a failed individual command. Require
    # one inode result per request before associating modes with the paths.
    if result.returncode or len(entries) != len(paths):
        raise ValueError("ext4 inode read incomplete: expected {} paths, got {}\n{}".format(
            len(paths), len(entries), result.stderr.decode("utf-8", errors="replace")[-4000:]))
    modes = {}
    for path, (kind, mode) in zip(paths, entries):
        expected_kind = {"file": "regular", "symlink": "symlink"}[records[path]["type"]]
        if kind != expected_kind:
            raise ValueError("ext4/readback type differs at {}: image={}, readback={}".format(path, kind, expected_kind))
        value = int(mode, 8)
        if value > 0o7777:
            raise ValueError("invalid ext4 permission mode at " + path)
        modes[path] = value
    return modes


def inventory_difference(expected, actual):
    differences = []
    for path in sorted(set(expected) | set(actual)):
        if path not in expected:
            differences.append(path + ": unexpected image path")
        elif path not in actual:
            differences.append(path + ": missing image path")
        elif expected[path] != actual[path]:
            fields = []
            for field in sorted(set(expected[path]) | set(actual[path])):
                before, after = expected[path].get(field), actual[path].get(field)
                if before == after:
                    continue
                if field == "mode":
                    before = "{:05o}".format(before) if isinstance(before, int) else repr(before)
                    after = "{:05o}".format(after) if isinstance(after, int) else repr(after)
                fields.append("{} expected={} actual={}".format(field, before, after))
            differences.append(path + ": " + "; ".join(fields))
    return "{} differing paths\n{}".format(len(differences), "\n".join(differences[:20]))


def verify(root, require_buildroot=False, rootfs_image=None):
    manifest = json.loads((root / MANIFEST.lstrip("/")).read_text())
    records = inventory(root)
    if rootfs_image is not None:
        for path, mode in ext4_file_modes(rootfs_image, records).items():
            records[path]["mode"] = mode
    if manifest.get("schema") != 1:
        raise ValueError("unsupported image inventory schema")
    if manifest.get("files") != records:
        raise ValueError("final rootfs content/mode differs from its image inventory: " +
                         inventory_difference(manifest.get("files", {}), records))
    if require_buildroot and manifest.get("ownership_mode") != "buildroot":
        raise ValueError("production image requires Buildroot preinstalled-package records")
    digest = hashlib.sha256(json.dumps(records, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if manifest.get("version") != "1+" + digest:
        raise ValueError("image base inventory digest differs")
    packages, owners = manifest["installed_packages"], manifest["owners"]
    if not {BASE, ABI}.issubset(packages) or set(owners) != set(records):
        raise ValueError("incomplete installed package ownership")
    if packages[BASE]["Version"] != manifest["version"] or packages[ABI]["Version"] != ABI_VERSION:
        raise ValueError("installed image/ABI identity differs")
    if set(owners.values()) - set(packages):
        raise ValueError("image file references an uninstalled package")
    status = []
    for name, fields in sorted(packages.items()):
        if not NAME.fullmatch(name) or fields["Package"] != name:
            raise ValueError("invalid installed package name")
        paths = {path for path, owner in owners.items() if owner == name}
        if name == BASE:
            paths.add(MANIFEST)
        expected_list = "".join(path + "\n" for path in aliased_paths(records, paths))
        info = root / "var/lib/opkg/info" / name
        if Path(str(info) + ".list").read_text() != expected_list:
            raise ValueError("installed file list differs: " + name)
        # JSON sorts keys; compare fields without imposing another key order
        # on the generated opkg control records.
        expected_fields = dict(fields, Essential="yes", Status="install hold installed")
        control = Path(str(info) + ".control").read_text()
        parsed = dict(line.split(": ", 1) for line in control.splitlines())
        if parsed != expected_fields or len(control.splitlines()) != len(expected_fields):
            raise ValueError("installed package control differs: " + name)
        status.append(control)
    if (root / "var/lib/opkg/status").read_text() != "\n".join(status) + "\n":
        raise ValueError("installed package status differs")
    print("TDVP opkg final rootfs: PASS {} paths, {} installed packages".format(len(records), len(packages)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-root", required=True, type=Path)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--build-info", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--require-buildroot", action="store_true")
    parser.add_argument("--rootfs-image", type=Path, help="read actual permission modes from this ext4 image")
    args = parser.parse_args()
    try:
        if args.verify:
            verify(args.target_root, args.require_buildroot, args.rootfs_image)
        else:
            if args.rootfs_image:
                parser.error("--rootfs-image requires --verify")
            seed(args.target_root, args.catalog, args.build_info, args.build_dir)
    except (KeyError, ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, "TDVP opkg image seed failed: {}\n".format(error))


if __name__ == "__main__":
    main()

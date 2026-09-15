#!/usr/bin/env python3
"""Check SDK payload, image binding, CPU0 ELF attributes and cross compilation."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import stat
import subprocess
import tempfile


TRIPLE = "riscv64-unknown-linux-gnu"
ARCH = "rv64imafdc_zicsr_zifencei"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(arguments, **kwargs):
    return subprocess.check_output([str(arg) for arg in arguments], stderr=subprocess.STDOUT, **kwargs).decode()


def verify_tree(root, manifest):
    if manifest.get("schema") != 1 or manifest.get("kind") != "tdvp-cpu0-application-sdk":
        raise ValueError("unsupported SDK manifest")
    if manifest.get("march") != ARCH or manifest.get("mabi") != "lp64d" or manifest.get("target") != TRIPLE:
        raise ValueError("unexpected CPU0 SDK policy")
    expected = manifest["files"]
    actual = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            path = Path(directory) / name
            relative = path.relative_to(root).as_posix()
            if relative == "tdvp-sdk-manifest.json":
                continue
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                # Runtime-only links such as /etc/mtab -> /proc/mounts may be
                # dangling in a development sysroot, but must stay within it.
                if not path.resolve().is_relative_to(root):
                    raise ValueError("SDK link escapes: " + relative)
                actual[relative] = {"type": "symlink", "target": os.readlink(path)}
            elif stat.S_ISREG(mode):
                actual[relative] = {"type": "file", "sha256": sha256(path), "mode": stat.S_IMODE(mode)}
            elif not stat.S_ISDIR(mode):
                raise ValueError("SDK special file: " + relative)
    if expected != actual:
        differences = [name for name in sorted(set(expected) | set(actual)) if expected.get(name) != actual.get(name)]
        raise ValueError("SDK payload differs: " + ", ".join(differences[:20]))
    digest = hashlib.sha256(json.dumps(actual, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if digest != manifest["files_sha256"]:
        raise ValueError("SDK inventory digest differs")
    for name, digest in manifest["image_bindings"].items():
        if "/" in name or name in (".", ".."):
            raise ValueError("invalid image binding filename")
        if not name.endswith(".img.gz") and sha256(root / "metadata" / name) != digest:
            raise ValueError("SDK image metadata differs: " + name)
    for name, digest in manifest["image_library_sha256"].items():
        path = root / "sysroot" / name.lstrip("/")
        if not path.resolve().is_relative_to(root / "sysroot") or sha256(path) != digest:
            raise ValueError("SDK final-image library differs: " + name)


def verify_elf(root, path, *, runtime_rvv=False):
    reader = root / "bin" / (TRIPLE + "-readelf")
    header = run([reader, "-h", path])
    if "ELF64" not in header or "RISC-V" not in header or "double-float ABI" not in header:
        raise ValueError("ELF is not CPU0 RISC-V ELF64/lp64d: " + str(path))
    attributes = run([reader, "-A", path])
    match = re.search(r'Tag_RISCV_arch:\s*"([^"]+)"', attributes)
    if not match:
        raise ValueError("ELF has no RISC-V ISA attributes: " + str(path))
    extensions = {re.sub(r"\d+p\d+$", "", token) for token in match[1].split("_")}
    allowed = {"rv64i", "m", "a", "f", "d", "c", "zicsr", "zifencei", "zmmul", "zaamo", "zalrsc"}
    if runtime_rvv:
        # Only verify_image_dependency can grant this to the reviewed Pixman
        # build. CLI --elf and every newly compiled application stay scalar.
        allowed |= {"v", "zve32f", "zve32x", "zve64d", "zve64f", "zve64x", "zvl32b", "zvl64b", "zvl128b"}
    if not extensions <= allowed or "rv64i" not in extensions:
        raise ValueError("ELF requires unsupported CPU0 ISA: " + str(path) + ": " + match[1])
    dynamic = run([reader, "-d", path])
    if "(RPATH)" in dynamic or "(RUNPATH)" in dynamic:
        search_paths = re.findall(r"\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]", dynamic)
        # Some final-image libraries retain an empty DT_RUNPATH tag after
        # Buildroot sanitization. It contains no runtime search directory.
        if not search_paths or any(search_paths):
            raise ValueError("application ELF embeds a runtime search path: " + str(path))
    return sorted(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic))


def verify_image_dependency(root, library, manifest):
    library = library.resolve()
    image_path = "/" + library.relative_to(root / "sysroot").as_posix()
    if manifest["image_library_sha256"].get(image_path) != sha256(library):
        raise ValueError("sample dependency is not a verified final-image library: " + image_path)
    runtime_rvv = image_path == "/usr/lib/libpixman-1.so.0.44.2"
    if runtime_rvv:
        packages = json.loads((root / "metadata/tdvp-buildroot-packages.json").read_text())
        if packages.get("pixman", {}).get("version") != "0.44.2":
            raise ValueError("Pixman runtime-dispatch review requires version 0.44.2")
    return verify_elf(root, library, runtime_rvv=runtime_rvv)


def verify_bundle(root, bundle, manifest):
    if sha256(root / "tdvp-sdk-manifest.json") != sha256(bundle / "tdvp-sdk-manifest.json"):
        raise ValueError("release/SDK manifest differs")
    for name, digest in manifest["image_bindings"].items():
        if sha256(bundle / name) != digest:
            raise ValueError("release/SDK binding differs: " + name)


def smoke_pixman_without_rvv(root, work, cc):
    # Pixman 0.44.2 gates its vector implementation on getauxval(AT_HWCAP).
    # Check the final-image library under an emulator which does not expose V.
    # This exercises initialization/compositing, not all library entry points
    # or GPU/board behavior. See docs/cpu0-application-sdk.md for the review.
    source = work / "pixman-scalar.c"
    source.write_text('''#include <pixman.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/auxv.h>
int main(void) {
    if (getauxval(AT_HWCAP) & (1UL << ('V' - 'A'))) return 10;
    if (pixman_version() != 4402) return 11;
    uint32_t src[64], dst[64] = {0};
    for (int i = 0; i < 64; ++i) src[i] = 0xff123456U;
    pixman_image_t *s = pixman_image_create_bits(PIXMAN_a8r8g8b8, 8, 8, src, 32);
    pixman_image_t *d = pixman_image_create_bits(PIXMAN_a8r8g8b8, 8, 8, dst, 32);
    if (!s || !d) return 12;
    pixman_image_composite32(PIXMAN_OP_OVER, s, 0, d, 0, 0, 0, 0, 0, 0, 8, 8);
    for (int i = 0; i < 64; ++i) if (dst[i] != src[i]) return 13;
    pixman_image_unref(s); pixman_image_unref(d);
    puts("Pixman runtime dispatch: PASS HWCAP.V=0, image library composite pixels correct");
    return 0;
}
''')
    flags = shlex.split(run([root / "bin/pkg-config", "--cflags", "--libs", "pixman-1"]))
    binary = work / "pixman-scalar.elf"
    run([cc, "-O2", source, "-o", binary] + flags)
    verify_elf(root, binary)
    env = dict(os.environ)
    for name in list(env):
        if name.startswith(("QEMU_", "PIXMAN_")) or name in ("LD_PRELOAD", "LD_LIBRARY_PATH"):
            del env[name]
    print(run(["qemu-riscv64", "-cpu", "rv64,v=false", "-L", root / "sysroot",
               "-E", "LD_LIBRARY_PATH=" + str(root / "sysroot/usr/lib"), binary], env=env, timeout=30).strip())


def smoke(root):
    cc = root / "bin" / (TRIPLE + "-gcc")
    cxx = root / "bin" / (TRIPLE + "-g++")
    if Path(run([cc, "-print-sysroot"]).strip()).resolve() != root / "sysroot":
        raise ValueError("compiler selected a foreign sysroot")
    for flags in ([], ["-mtune=c908"], ["-mcpu=c908"], ["-mcpu=c908v"]):
        macros = run([cc] + flags + ["-dM", "-E", "-x", "c", "-"], input=b"")
        if "#define __riscv_vector " in macros or "#define __riscv_xlen 64" not in macros:
            raise ValueError("compiler default is not CPU0 scalar")
    with tempfile.TemporaryDirectory(prefix="tdvp-sdk-smoke-") as temporary:
        work = Path(temporary)
        sources = {
            "hello.c": '#include <stdio.h>\n#include <pthread.h>\nint main(void) { puts("TDVP CPU0"); return pthread_self() == 0; }\n',
            "hello.cpp": '#include <iostream>\n#include <vector>\nint main() { std::vector<int> v{1,2,3}; std::cout << v.at(1) << std::endl; }\n',
            "desktop.c": '#include <gtk/gtk.h>\n#include <libmount/libmount.h>\n#include <wayland-client.h>\nint main(void) { struct libmnt_table *t = mnt_new_table(); mnt_free_table(t); return gtk_get_major_version() + (wl_display_connect(0) != 0); }\n'}
        needed = set()
        for name, source in sources.items():
            path = work / name
            path.write_text(source)
            binary = work / (path.stem + ".elf")
            flags = ["-O2", "-D_FORTIFY_SOURCE=1", "-Wl,-rpath-link," + str(root / "sysroot/usr/lib")]
            libraries = ["-pthread"]
            if name == "desktop.c":
                libraries += shlex.split(run([root / "bin/pkg-config", "--cflags", "--libs", "gtk+-3.0", "mount", "wayland-client"]))
            run([cxx if name.endswith(".cpp") else cc] + flags + [path, "-o", binary] + libraries)
            needed.update(verify_elf(root, binary))
        # An explicitly overridden ISA must still be rejected by the ELF gate.
        vector = work / "vector.o"
        run([cc, "-march=rv64gcv", "-c", work / "hello.c", "-o", vector])
        try:
            verify_elf(root, vector)
        except ValueError as error:
            if "unsupported CPU0 ISA" not in str(error):
                raise
        else:
            raise ValueError("ELF gate accepted RVV")
        manifest = json.loads((root / "tdvp-sdk-manifest.json").read_text())
        verified = set()
        while needed - verified:
            name = sorted(needed - verified)[0]
            candidates = [root / "sysroot/usr/lib" / name, root / "sysroot/lib" / name]
            library = next((path.resolve() for path in candidates if path.is_file()), None)
            if library is None:
                raise ValueError("sample dependency absent from image sysroot: " + name)
            needed.update(verify_image_dependency(root, library, manifest))
            verified.add(name)
        smoke_pixman_without_rvv(root, work, cc)
        (work / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.16)\nproject(tdvp_sdk_smoke C CXX)\nadd_executable(cmake-c hello.c)\nadd_executable(cmake-cxx hello.cpp)\n')
        run(["cmake", "-S", work, "-B", work / "build", "-DCMAKE_TOOLCHAIN_FILE=" + str(root / "toolchain.cmake")])
        run(["cmake", "--build", work / "build", "-j2"])
        verify_elf(root, work / "build/cmake-c")
        verify_elf(root, work / "build/cmake-cxx")
    print("TDVP SDK smoke: PASS C, C++, GTK/libmount/Wayland, CMake, scalar defaults, RVV rejection and {} image dependencies".format(len(verified)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--elf", type=Path)
    args = parser.parse_args()
    try:
        root = args.sdk.resolve()
        manifest = json.loads((root / "tdvp-sdk-manifest.json").read_text())
        verify_tree(root, manifest)
        if args.bundle:
            verify_bundle(root, args.bundle, manifest)
        if args.elf:
            verify_elf(root, args.elf)
        if args.smoke:
            smoke(root)
        print("TDVP SDK verification: PASS {} paths, {} final-image libraries".format(len(manifest["files"]), len(manifest["image_library_sha256"])))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        detail = error.output.decode(errors="replace") if isinstance(error, subprocess.CalledProcessError) and error.output else ""
        parser.exit(1, "TDVP SDK verification failed: {}\n{}\n".format(error, detail))


if __name__ == "__main__":
    main()

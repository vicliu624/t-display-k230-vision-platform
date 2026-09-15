#!/usr/bin/env python3
"""Additional isolated consumer tests for the actual exported SDK."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def run(arguments, **kwargs):
    return subprocess.check_output([str(arg) for arg in arguments], stderr=subprocess.STDOUT, text=True, **kwargs)


def check(root):
    spec = importlib.util.spec_from_file_location("sdk_verify", root / "verify-sdk.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    cc = root / "bin/riscv64-unknown-linux-gnu-gcc"
    pkg_config = root / "bin/pkg-config"
    for name in ("libgcc.a", "crtbeginS.o", "crt1.o", "libstdc++.so"):
        path = Path(run([cc, "-print-file-name=" + name]).strip()).resolve()
        if not path.is_file() or not path.is_relative_to(root):
            raise ValueError("compiler support file is outside SDK: " + str(path))

    packages = sorted({line.split()[0] for line in run([pkg_config, "--list-all"]).splitlines() if line})
    if len(packages) < 20:
        raise ValueError("unexpectedly small pkg-config inventory")
    for package in packages:
        flags = shlex.split(run([pkg_config, "--cflags", "--libs", package]))
        for flag in flags:
            if flag.startswith(("-I/", "-L/")) and not Path(flag[2:]).resolve().is_relative_to(root / "sysroot"):
                raise ValueError("host include/library path leaked from {}: {}".format(package, flag))
            if "-rpath," in flag or "-rpath=" in flag:
                raise ValueError("runtime path leaked from pkg-config: " + package)

    with tempfile.TemporaryDirectory(prefix="tdvp-sdk-extra-") as temporary:
        work = Path(temporary)
        (work / "part.c").write_text("int answer(void) { return 42; }\n")
        (work / "main.c").write_text("extern int answer(void); int main(void) { return answer() != 42; }\n")
        run([cc, "-O2", "-flto", "-c", work / "part.c", "-o", work / "part.o"])
        run([root / "bin/riscv64-unknown-linux-gnu-gcc-ar", "rcs", work / "libpart.a", work / "part.o"])
        run([root / "bin/riscv64-unknown-linux-gnu-gcc-ranlib", work / "libpart.a"])
        run([cc, "-O2", "-flto", work / "main.c", work / "libpart.a", "-o", work / "lto.elf"])
        verifier.verify_elf(root, work / "lto.elf")

        (work / "desktop.c").write_text('#include <gtk/gtk.h>\n#include <libmount/libmount.h>\n#include <wayland-client.h>\nint main(void) { return gtk_get_major_version() + (mnt_new_table() != 0) + (wl_display_connect(0) != 0); }\n')
        (work / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.16)\nproject(tdvp_sdk_consumer C)\nfind_package(PkgConfig REQUIRED)\npkg_check_modules(DESKTOP REQUIRED IMPORTED_TARGET gtk+-3.0 mount wayland-client)\nadd_executable(desktop desktop.c)\ntarget_link_libraries(desktop PkgConfig::DESKTOP)\n')
        run(["cmake", "-S", work, "-B", work / "build", "-DCMAKE_TOOLCHAIN_FILE=" + str(root / "toolchain.cmake")])
        run(["cmake", "--build", work / "build", "-j2"])
        verifier.verify_elf(root, work / "build/desktop")
        for mode in ("--libs", "--cflags"):
            clean = run([pkg_config, mode, "gtk+-3.0"])
            env = dict(os.environ, PKG_CONFIG_PATH="/usr/lib/pkgconfig", PKG_CONFIG_LIBDIR="/usr/lib/pkgconfig",
                       PKG_CONFIG_SYSROOT_DIR="/foreign", LD_LIBRARY_PATH="/foreign")
            if run([pkg_config, mode, "gtk+-3.0"], env=env) != clean:
                raise ValueError("pkg-config accepted foreign environment paths")
    print("TDVP SDK consumer checks: PASS {} pkg-config modules, support files, LTO archive/link, CMake imported targets and foreign-env rejection".format(len(packages)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    args = parser.parse_args()
    try:
        check(args.sdk.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.output if isinstance(error, subprocess.CalledProcessError) else ""
        parser.exit(1, "TDVP SDK consumer checks failed: {}\n{}\n".format(error, detail))


if __name__ == "__main__":
    main()

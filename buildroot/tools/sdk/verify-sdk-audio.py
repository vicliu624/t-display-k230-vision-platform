#!/usr/bin/env python3
"""Compile and optionally execute the actual paired libsndfile codec contract."""
import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def check_audio(sdk, execute):
    source = Path(__file__).with_name("audio-codec-smoke.c")
    spec = importlib.util.spec_from_file_location("paired_sdk_verifier", sdk / "verify-sdk.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    with tempfile.TemporaryDirectory(prefix="tdvp-sdk-audio-") as temporary:
        binary = Path(temporary) / "audio-codec-smoke"
        flags = shlex.split(subprocess.check_output(
            [str(sdk / "bin/pkg-config"), "--cflags", "--libs", "sndfile"], text=True))
        subprocess.run([str(sdk / "bin/riscv64-unknown-linux-gnu-gcc"), "-O1", str(source),
                        "-o", str(binary), "-Wl,-rpath-link," + str(sdk / "sysroot/usr/lib")]
                       + flags + ["-lm"], check=True)
        verifier.verify_elf(sdk, binary)
        if execute:
            qemu = shutil.which("qemu-riscv64")
            if qemu is None:
                raise ValueError("audio runtime acceptance requires qemu-riscv64")
            # Never accept caller-supplied LD_LIBRARY_PATH masking image codecs.
            env = dict(os.environ, LD_LIBRARY_PATH=str(sdk / "sysroot/usr/lib") + ":" + str(sdk / "sysroot/lib"))
            subprocess.run([qemu, "-L", str(sdk / "sysroot"), str(binary)], env=env, check=True)
        print("SDK audio consumer compile/CPU0 ELF: PASS" + ("; paired codec runtime: PASS" if execute else "; runtime not tested"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--run", action="store_true")
    arguments = parser.parse_args()
    try:
        check_audio(arguments.sdk.resolve(strict=True), arguments.run)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, "SDK audio codec acceptance failed: " + str(error) + "\n")


if __name__ == "__main__":
    main()

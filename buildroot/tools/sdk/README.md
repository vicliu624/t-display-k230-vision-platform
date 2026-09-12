# TDVP CPU0 application SDK

This archive pairs a specific image with its cross compiler, development
sysroot and preinstalled-package inventory. It targets CPU0 Linux, using
`rv64imafdc_zicsr_zifencei` and `lp64d`. CPU1 firmware/model development uses
its separate RT-Smart toolchain and is outside this SDK's scope.

## Host and activation

Validated host: Ubuntu 24.04 x86_64. Install native `build-essential`, `python3`,
`cmake`, `make`, `binutils`, `zlib1g`, `gzip`, `tar` and `qemu-user` (for the
Pixman check in `--smoke`). Applications can require
additional native generators; these are not target executables from sysroot.
The original build tree and `/opt/toolchain` are not needed.

Verify the release's SHA256SUMS before extracting the archive. Extract as an
ordinary user into a path without spaces or shell metacharacters, then:

```sh
python3 /path/to/tdvp-sdk/verify-sdk.py /path/to/tdvp-sdk --smoke
source /path/to/tdvp-sdk/environment-setup.sh
$CC $CFLAGS hello.c $LDFLAGS -o hello
$CXX $CXXFLAGS hello.cpp $LDFLAGS -o hello-cxx
cmake -S app -B app-build -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE"
cmake --build app-build
```

The compiler/sysroot paths are relative to the SDK. After moving the directory,
source environment-setup.sh again and use a fresh application build directory.
pkg-config ignores host search paths and uses this sysroot. Prefer pkg-config
over target-side legacy `*-config` executables. The toolchain's internal
`toolchain/bin` directory is not a supported compiler entry point.

The wrapper retains Buildroot's stack, PIE and RELRO defaults. Scalar ISA
defaults also apply when an application specifies only `-mtune` or `-mcpu`.
Explicit ISA/ABI overrides and hand-written assembly remain the application's
responsibility; run `verify-sdk.py SDK --elf FILE` on each application ELF.
The package feed must additionally inspect every ELF in its dependency closure,
file ownership and maintainer scripts before publication.

## Image identity and limits

`tdvp-sdk-manifest.json` binds the compiler policy and complete SDK file
inventory to image, rootfs metadata and opkg inventory hashes. `metadata/`
contains the exact release copies. Use `--bundle /path/to/release` to verify
the binding against all companion release files, including the compressed image.
Final-image shared libraries replace staging copies where both contain the
same regular `/usr/lib/*.so*` path; their exact hashes are recorded separately.
Headers, static libraries and linker scripts come from that build's staging
tree. Development metadata may have paths normalized for relocation.
SDK copies use ordinary readable host permissions and carry no setuid/setgid
bits. Image permissions and image files are left unchanged.

Keep image-owned runtime packages held and essential. SDK success does not
authorize replacing the base system's libraries. Application package versions
and dependencies must be generated from the published image inventory.
Firmware/device acceptance and NetSurf installation/reboot acceptance are
separate checks. A temporary Actions artifact is not a published feed baseline.

The scalar ELF rule has one reviewed image-library exception: Pixman 0.44.2
contains optional RVV code selected through Linux HWCAP.V. Its version and
final-image hash must match the paired metadata. The smoke test also runs an
8x8 compositing check with QEMU's V extension disabled and asserts HWCAP.V=0.
New application ELFs and CLI `--elf` checks have no RVV exception. This focused
check does not prove every image-library code path or emulate the K230 board.

Upstream compiler documentation is retained in `toolchain/share`. The exact
Buildroot wrapper source and its COPYING file are in
`share/licenses/buildroot-wrapper`. Selected package source versions and staged
SDK inputs are recorded in metadata; preserve these and the matching project's
source locks when distributing the SDK. This archive does not claim to be a
complete Buildroot legal-info/source bundle.

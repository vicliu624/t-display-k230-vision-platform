# CPU0 application SDK

The release collector now exports `<release-name>-cpu0-sdk.tar.gz` alongside
the image. It uses the selected Buildroot output and the same final ext4.
Exporting does not run another image build or alter the SDK worktree.

## Contents and identity

- The pinned Xuantie GCC/binutils distribution, with its alternate vendor
  sysroots replaced by the image build's selected development sysroot.
- A rebuilt Buildroot compiler wrapper with relative paths, `lp64d` and scalar
  `rv64imafdc_zicsr_zifencei` defaults. Stack protection, PIE and RELRO remain.
- pkg-config and CMake entry points, plus a Bash environment setup script.
- Exact final-image copies of shared libraries present at matching regular
  `/usr/lib/*.so*` staging paths. Headers, static libraries and linker scripts
  retain their build provenance. SDK copy permissions are ordinary host modes.
- Image/package metadata and a complete SDK file/hash inventory. The external
  `tdvp-sdk-manifest.json` is identical to the archive's internal copy.

The JSON manifest binds the compressed image, image manifest, staged source
manifest, preinstalled package records and selected Buildroot package versions.
It also records the resolved build configuration hash, exporter hash, compiler
version, target ISA/ABI and final-image library hashes. `SHA256SUMS` includes
both SDK archive and JSON manifest.

## Export and validate locally

Finish the normal image build, then run `collect-release-bundle.sh` as described
in the getting-started guide. To reproduce the CI handoff check:

```sh
docker build -t tdvp-sdk-validation:ubuntu24.04 \
  -f buildroot/tools/sdk/Dockerfile.validation buildroot/tools/sdk
bash buildroot/tools/test-tdvp-sdk-relocation.sh \
  output/RELEASE/RELEASE-cpu0-sdk.tar.gz output/RELEASE
```

The validator verifies release hashes, then mounts the SDK at two different
paths in Ubuntu 24.04. It uses an ordinary UID and read-only mounts, disables
network access, hides `/opt`, and does not mount the original build tree.
C/C++, GTK/libmount/Wayland and CMake builds must pass. Application ELF checks reject RVV,
unsupported ISA attributes, wrong ABI and embedded runtime search paths;
sample dependencies are checked recursively against final-image library hashes.
CI performs this before artifact upload or tagged Release publication.

Pixman 0.44.2 is a reviewed exception for image-library ISA metadata: its
`pixman-riscv.c` selects the optional vector implementation only when Linux
reports HWCAP.V. The dependency check requires that exact package version,
library path and paired final-image hash; it permits the standard RVV subset,
with no vendor-ISA exception. A QEMU check disables V, verifies HWCAP.V=0, and
runs an 8x8 composite against the image's library with pixel assertions.
This verifies the tested scalar dispatch path. It does not cover every Pixman
entry point or K230 hardware, and does not change the VGLite desktop renderer.
The CLI `--elf` check and new application binaries remain strictly scalar.

The additional consumer checks resolve every exported pkg-config module,
exercise LTO archives and CMake imported GTK/Wayland targets, and reject host
pkg-config environment contamination. Offline regressions cover mismatched
images/manifests, changed payload/modes, escaping symlinks, repeated exports,
unsupported ISA/ABI and nonempty runtime library search paths.

## Application use and boundaries

Extract to a path without spaces or shell metacharacters. Follow the archive's
README, source `environment-setup.sh`, and use its compiler/pkg-config or CMake
toolchain file. Moving the SDK requires re-sourcing the environment and a fresh
application build directory. Native generators belong to the host; do not run
target executables from sysroot as build tools.

This is the CPU0 application SDK. CPU1 firmware and AI model development retain
their separate toolchain. Explicit compiler ISA overrides, assembly, package
maintainer scripts and a full application dependency closure require feed-side
checks. This handoff test does not run the compiled applications on hardware.

The first complete archive is still a candidate until published in a tagged
Release with an accepted image. Public feeds must lock that release's hashes.
NetSurf installation, HTTPS browsing and reboot acceptance remain separate work.
The SDK retains vendor documentation and the wrapper's exact source/license;
it does not claim to include a complete Buildroot legal-info/source bundle.

Implementation references: [Buildroot SDK export](https://buildroot.org/downloads/manual/manual.html#_exporting_the_sdk)
and [GCC sysroot/path options](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html).

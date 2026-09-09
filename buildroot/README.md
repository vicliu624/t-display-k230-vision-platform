# Buildroot and SDK Worktree

The project stages a pinned K230 Linux SDK into an ext4 worktree and overlays
the board profile, packages, kernel fragments, and image scripts from this
repository. The active Buildroot configuration is:

```text
k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## Workflow

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$HOME/work/tdvp-k230-labwc"
```

Run these commands on a Linux/ext4 worktree (native Linux, a container, or WSL).
For CI parity, use Ubuntu 24.04 x86_64 and the host preparation script used by
`.github/workflows/ci.yml`; a successful build on an older WSL distribution is
not equivalent to verification in the CI userspace. The project checkout may live
on the Windows filesystem, but `$HOME/work/tdvp-k230-labwc` is the only
disposable build input. Do not invoke `make` inside the vendor SDK or use an
`output/<profile>` directory as the worktree argument.

The first command creates the SDK worktree, copies the project overlay and
user-space package sources, normalizes text build inputs to LF, writes a
source manifest, and runs the staged-package assertion. It fails before any
compile if the external package graph, copied package source, or required
desktop input differs from the project source.

The second command verifies the staged manifest against the current project
again before it synchronizes the vendor SDK and builds the kernel, root
filesystem, boot partition, and complete SD image. The third command audits
the generated image contents, fixed boot offsets, filesystem identities,
desktop session, network recovery tools, and selected board services.

### Build Contract

The build is accepted only when each boundary succeeds in this order:

```text
project source
  -> ext4 staged overlay and user-space sources
  -> synchronized vendor SDK Buildroot input
  -> generated rootfs and SD-card image
  -> image assertion
```

- The staging script compares a content manifest for every `user-space/*/src`
  tree after copying it into the external package overlay.
- The build script compares the current project manifest with the staged
  manifest and refuses stale worktrees.
- Text build inputs are pinned to LF by `.gitattributes`; binary assets retain
  their original bytes.
- A core patch or package-graph change discards the generated Buildroot output
  before configuration. Source-only changes retain an incremental package path
  only after a fresh stage.
- Deployment and release publication consume only an image that has passed the
  final image assertion. A remote experiment is never treated as an image
  artifact.

For a fast, non-compiling decision about the next build, use:

```sh
TDVP_STAGE_DRY_RUN=1 \
  bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
```

### Check delivery rules before compiling

```sh
bash buildroot/tools/test-tdvp-image-source-contract.sh
bash buildroot/tools/test-tdvp-session-idle-contract.sh
bash buildroot/tools/test-tdvp-renderer-stack-lock.sh
bash buildroot/tools/test-tdvp-cpu1-vision-status.sh
```

The source-contract test derives installation paths from the greeter and desktop
package recipes, copies the real source files into a temporary ext4 filesystem,
and runs the corresponding assertions and extraction helpers from the production
image verifier. It reports all mismatches and rejects deliberately broken login
commands, users, and session launchers. It needs Bash, Python 3, and e2fsprogs,
but no compiler, mount, root privileges, or existing SDK output.

It also executes the literal metadata statements from the real post-image
manifest writer and checks the release baseline's literal requirements. The
wlroots/Labwc revisions and renderer policy fields are independently compared
with their package recipes and delivered environment, catching omitted or
incorrect manifest fields before the full build.

This fast check covers directly installed desktop policy files, not compiled
executables, generated system configuration, variable-valued manifest fields
and artifact hashes, complete partition contents, or
hardware behavior. Keep the independent CPU1 firmware preflight and the final
full-image/release guards. Record these as separate validation results; passing
one does not imply that the later stages have passed.

The CPU1 status regression also calls `test-tdvp-cpu1-hwctl-image-contract.sh`.
It compiles the production status object with C++17 and `-O0`, places it in a
temporary ext4 filesystem, runs the image verifier's actual `vpl-hwctl` content
assertions, and creates a negative control for every rule. This step requires a
C++17 compiler and checks the interface identifiers in the compiled payload.
Pass an existing cross-compiled `vpl-hwctl` as that script's sole argument to
check a real target ELF. It does not execute RISC-V code or replace full-image
and hardware acceptance.

## Persistent Inputs

- `sdk-sources.lock`: pinned SDK and toolchain inputs.
- `patches/`: core Buildroot and Linux patch queues.
- `k230-sdk-overlay/`: board files, package recipes, configuration fragments,
  and image hooks.
- `tools/`: staging, build, assertion, release, and host setup scripts.

The resulting artifacts are under:

```text
$WORKTREE/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/
```

See [SDK Baseline](SDK_BASELINE.md) and
[Overlay Guide](k230-sdk-overlay/README.md).

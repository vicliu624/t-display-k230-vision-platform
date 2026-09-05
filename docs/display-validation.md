# Display Validation

The image validates the internal RM69A10 panel through the K230 DRM/KMS path.
The fixed-panel approved candidate uses connector `DSI-1`, output transform
`90`, and logical size `1232x568`; the current board session has been observed
with `WLR_RENDERER=vglite`. Pixman is the release/recovery baseline, not an
automatic interpretation of an ordinary SHM benchmark pass.

## Image Checks

The image assertion verifies these installed artifacts:

- `/dev/dri/card0` session contract through greetd and the authenticated Labwc
  session.
- `seatd`, `labwc`, `swaybg`, `wf-panel-pi`, its upstream `wfplug-*`
  modules, `pcmanfm`, `foot`, and `wlr-randr`.
- `/etc/tdvp/labwc/environment` with the K230 output values.
- `/etc/xdg/labwc/autostart` launching Swaybg, PCManFM desktop handling, and
  wf-panel-pi.
- GT9895 libinput calibration rule.

Run the host-side check after a build:

```sh
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
```

## Device Checks

On the device:

```sh
systemctl status seatd greetd
pgrep -a labwc
cat /sys/class/drm/card0-DSI-1/status
```

The desktop reaches the panel through Labwc. The Raspberry Pi-maintained
wf-panel-pi and its upstream plugins provide the application menu, status
modules, and window list. Foot provides the terminal recovery path.

For a compositor incident, collect the current and previous Labwc session
logs before restarting the desktop:

```sh
cat "$XDG_RUNTIME_DIR/tdvp-labwc.log"
cat "$XDG_RUNTIME_DIR/tdvp-labwc.log.previous"
```

The session launcher rotates exactly one prior log and records the compositor
group exit status, so libinput, wlroots, and client startup errors do not rely
on the login VT remaining visible.

## Maintenance-mode KMS acceptance

`tdvp-display-smoke` obtains DRM master and performs direct modesets/atomic
commits. Do **not** run it while greetd, Labwc, or a graphical desktop owns
the card; it is not a desktop health probe. The low-level
`tdvp-kms-acceptance` wrapper refuses to start when either process is present.
For page-flip validation, use the separately named maintenance transaction:

```sh
sudo systemctl start tdvp-kms-maintenance
sudo systemctl --no-pager status tdvp-kms-maintenance
sudo cat /run/tdvp/acceptance/kms-maintenance.status
sudo cat /run/tdvp/acceptance/kms-maintenance.log
sudo cat /run/tdvp/acceptance/kms.status
sudo cat /run/tdvp/acceptance/kms.log
```

The manually started maintenance service records whether it stopped greetd,
waits for Labwc to exit, then runs a bounded two-dumb-buffer `XR24` page-flip
test followed by a 120-frame vblank observer after the direct KMS client has
closed. In this deliberately isolated transaction, `card0` has no graphical
owner, so opening the primary node can make the observer DRM master. The
wrapper therefore supplies an explicit maintenance-only opt-in; the observer
still refuses DRM master by default, and never modesets or page-flips. The
first test proves the previous framebuffer is not
rewritten before its event; the second verifies that
`enable_vblank → VO IRQ1 → drm_crtc_handle_vblank` still has bounded cadence.
Either failure rejects the maintenance acceptance. `ExecStopPost` restores
greetd on both success and failure, without depending on the SSH client
remaining connected. Normal desktop validation checks greetd, Labwc, and
clients without opening `/dev/dri/card0` directly. `--help`/`-h` and unknown
arguments for the maintenance, acceptance, and recovery helpers are pure CLI
paths: they create no state and cannot stop greetd or acquire DRM. Direct
invocation is rejected; only the systemd-injected maintenance marker permits
the transaction that isolates the graphical session. Both unit names route to
that one guarded path.

One completed hardware transaction proved the page-flip lifecycle: the XR24
two-dumb-buffer counter submitted and released 300 frames, with sequence
`44042..44965`, steps `3..5`, submit-to-event
`0.239/7.796/23.279 ms`, followed by a 120-frame observer whose event interval
was `23945/23948.4/23952 us`. The previous counter called its requested
`300 / --fps` ten seconds `elapsed_seconds`; its measured event span was
`22104 ms` (about `13.57 fps`) because it includes a full-frame CPU fill and
sleep per iteration. It is not a KMS or VGLite submission-latency result. The
current candidate binary SHA-256 is
`4db5c18c7c241c3a50c92fc511418cdfcbcd5a39dae7005db638746a2e733145`; it labels
the old value `nominal_seconds` and additionally reports `wall_elapsed_ms` and
`achieved_fps`. A brief edge residue after the prior fullscreen counter has
cleared, so this documentation update deliberately does not rerun the DRM
transaction merely to refresh the reporting field; rerun only with panel
observation.

The host-side state-machine regression test does not require target hardware:

```sh
bash buildroot/tools/test-tdvp-kms-maintenance.sh
bash buildroot/tools/test-tdvp-vblank-observer-maintenance-master.sh
```

## Desktop SHM frame-callback acceptance

After the user has logged in to the normal Labwc desktop, run the session
wrapper:

```sh
tdvp-wayland-shm-bench-session --frames 120 --max-frame-ms 250
```

For a root-operated remote measurement, explicitly select the desktop account:

```sh
tdvp-wayland-shm-bench-session --user tdvp --frames 120 --max-frame-ms 250
```

The wrapper locates live Labwc, reads its actual `XDG_RUNTIME_DIR`, verifies
the Wayland socket, and drops to the session owner when started as root. It
does not assume `/run/user/<uid>`: the TDVP greetd session intentionally uses
a private user-owned runtime directory. The raw client remains available for
an already-correct desktop environment.

This opens one temporary 320x240 XDG toplevel, alternates between two
linearly addressed `wl_shm` buffers, and waits for 120 compositor frame
callbacks. It defaults to `XR24` (`WL_SHM_FORMAT_XRGB8888`); pass
`--format ar24` to create `AR24` (`WL_SHM_FORMAT_ARGB8888`) buffers with
non-opaque alpha. The client first requires the live `wl_shm` global to
advertise the selected format, so an unsupported alpha path fails explicitly.
It neither opens `/dev/dri/card0` nor takes DRM master, so it is the
ordinary-desktop health probe rather than a maintenance-mode KMS test. A
successful result reports minimum, average, and maximum callback latency; a
missing callback, compositor disconnect, buffer-release stall, unsupported
format, or callback beyond the configured bound exits nonzero.

Every run first prints `SCHED online_cpus=... loadavg=...`. Keep that line with
the result: a liveness pass under a saturated one-core system is not a desktop
performance acceptance. For a release baseline, close independent CPU-heavy
clients, run at least three consecutive 120-frame samples, and preserve the
three `SCHED` lines together with min/average/max latency. The `250 ms` bound
is a failure detector, not the final interactive-latency budget; establish the
budget only from clean, repeatable hardware samples after direct KMS page-flip
acceptance passes.

An ordinary SHM pass proves only that the fixed-panel SHM path through Labwc is
live; it does not identify the selected renderer. A VGLite conclusion requires
the Gate 1 checks below for the actual Labwc environment, profile, and
`/dev/vg_lite` ownership. It deliberately does **not** claim client DMA-BUF
import, explicit fences, modifier negotiation, HDR/color management, or
direct-scan-out coverage; those remain separate acceptance domains.

### Experimental VGLite Gate 1 (approved candidate sessions only)

The release image defaults to Pixman, so a normal SHM benchmark PASS is not
VGLite evidence. Only after an experimental image has been explicitly approved
for VGLite and a fresh graphical session is running that profile may the
graphical login user run:

```sh
tdvp-vglite-session-gate --expect-vglite-compositor \
  --width 1232 --height 568 --format xr24 --damage-size 0 --frames 120 --max-frame-ms 1000 --repeat 3
```

Before and after the workload, this command reads the actual Labwc process
environment. It requires `WLR_RENDERER=vglite`, the guarded three-consecutive-
render-failure recovery switch, and VGLite's direct-scan-out guard before it
runs the panel-logical-size two-buffer `wl_shm` benchmark followed by
per-client VGLite churn. On a candidate containing kernel patch 0059 it also
reads the loaded VGLite module's
`/sys/module/vglite/parameters/infinite_wait_watchdog_ms` parameter before the
first workload and after every churn round; the approved default is exactly
`5000` ms. This is a read-only identity and configuration check, not a
deliberate GPU-hang injection. It neither selects a renderer nor starts/stops
Labwc, and never opens DRM/KMS. `--repeat 3` repeats the selected SHM workload
and churn three times and revalidates Labwc, the profile, and the watchdog
parameter after every round. Run both `--format xr24` and `--format ar24`, each
with full damage (`--damage-size 0`) and the same geometry with small damage
(`--damage-size 64`); retain stdout/stderr, `$XDG_RUNTIME_DIR/tdvp-labwc.log`,
and kernel logs for every format/load pair. A missing, unreadable, or mismatched
watchdog parameter, callback or release timeout, unsupported format, churn
failure, Labwc exit, or automatic Pixman recovery is a Gate 1 failure; a
recovered Pixman desktop is not a VGLite pass.

### 2026-09-03 low-load hardware baseline

On the RM69A10 board running the deployed
`0053-tdvp-drm-canaan-page-flip-lifecycle.patch`, three consecutive 120-frame
samples were collected after the independent CPU-heavy `tdvp-sdr` client had
stopped. The candidate observers ran against the live Pixman/Labwc session.
The DRM-event timestamp interval averaged `23950.2 us` in all three runs
(about 41.75 Hz), with maxima of `23977 us`, `23962 us`, and `23978 us`.
The two-buffer linear `XR24` SHM client reported average frame callbacks of
`22851.7 us`, `22995.4 us`, and `22815.5 us`, with maxima of `41646 us`,
`36972 us`, and `36936 us`; every run returned `PASS`.

Keep this result separate from the sample taken while the CPU-intensive client
was present: that sample had a `103468.9 us` average and `355043 us` maximum
SHM callback while hardware event intervals remained tight. The A/B evidence
therefore attributes the touch-event lag and crash-like desktop unresponsiveness
to one-core userspace scheduling pressure, not to lost vblank or the known
page-flip lifecycle failure. It is still not exclusive-KMS dynamic page-flip
acceptance and cannot replace later DMA-BUF, fence, or direct-scan-out tests.

## Passive DRM vblank observer

For a kernel-side check that does not replace the desktop scan-out, run as
root while Labwc owns the card:

```sh
tdvp-vblank-observer --frames 120 --max-interval-ms 250
```

The observer submits only `DRM_VBLANK_EVENT` requests on the existing card
file descriptor. It reports `event_interval_us` from the timestamp carried by
the DRM event and `delivery_interval_us` from the monotonic time at which
userspace dispatched that event. Keep the two fields separate: a tight event
interval with a jittery delivery interval identifies CPU scheduling pressure,
not a missing hardware vblank. It does not modeset, page-flip, allocate
buffers, create a Wayland surface, or obtain DRM master; it refuses to run if
it unexpectedly becomes master. This isolates the
`enable_vblank → VO IRQ1 → drm_crtc_handle_vblank` path. It is intentionally
not evidence that the CRTC pending-page-flip event path is correct; the
managed KMS maintenance acceptance remains the proof for that path.

## Passive KMS format, modifier, and fence observer

For a no-master inventory of the KMS contract while Labwc owns the output, run
as root:

```sh
tdvp-kms-capability-observer --device /dev/dri/card0
```

This opens the card read-only, rejects DRM master, and only makes per-file
client-capability and property/blob queries. It does not allocate a buffer,
commit an atomic state, page-flip, or change a plane. The output records each
plane's XR24/AR24 support, `IN_FORMATS` modifier entries, plane
`IN_FENCE_FD`, the CRTC's `OUT_FENCE_PTR` capability, and the
`DRM_CAP_SYNCOBJ`/`DRM_CAP_SYNCOBJ_TIMELINE` capabilities.

On the fixed RM69A10 hardware, the active primary plane and two overlay planes
advertise XR24/AR24 with modifier `0` (linear), the relevant planes expose an
input-fence property, and the active CRTC exposes an output fence. However, the
same non-invasive observation on 2026-09-05 reported
`DRM_CAP_SYNCOBJ=0` and `DRM_CAP_SYNCOBJ_TIMELINE=0`. Plane/CRTC sync-file
properties therefore exist, but they do not make the timeline-syncobj backing
required by Wayland explicit synchronization available on this kernel.

This establishes only format and KMS-property prerequisites for a future
DMA-BUF/direct-scan-out design. It does **not** prove that wlroots imports
client DMA-BUFs, propagates acquire/release fences, negotiates modifiers, or
chooses a direct-scan-out plane. Future kernel work must validate syncobj
eventfd behavior, sync-file import/export, fence-fd ownership, and release
timing; compositor work must then pass end-to-end acceptance for every real
producer.

# Image-Owned User Space

This directory contains source owned by the image build:

| Source | Responsibility |
| --- | --- |
| `tdvp-labwc-desktop` | Systemd service and XDG configuration for the Labwc desktop session. It starts PCManFM desktop mode and the TDVP-themed wf-panel-pi top panel after Labwc has acquired the DRM backend; mouse-emulated touch retains left-click taps/drags and converts a stationary long press into a right click. |
| `tdvp-kpu-acceptance` | KPU runtime inspection and acceptance utility. |
| `vicliu-pocket-linux-hardware` | Board integration service and hardware-state publishing tools. |

Applications use normal Wayland and XDG conventions. The desktop recognizes
their standard `.desktop` entries; application code does not need to link to
the session source in this directory.

The Buildroot package recipes live in
`buildroot/k230-sdk-overlay/package/` and stage these source trees during the
SDK worktree preparation step.

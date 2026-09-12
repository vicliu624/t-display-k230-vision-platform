# Image-Owned User Space

This directory contains source owned by the image build:

| Source | Responsibility |
| --- | --- |
| `tdvp-greeter` | greetd login configuration and VGLite greeter for selected-account authentication. |
| `tdvp-labwc-desktop` | Authenticated VGLite/Labwc session, XDG configuration, PCManFM, wf-panel-pi and input. Session-scoped swayidle invokes PAM-backed gtklock, then wlopm blanks outputs. |
| `vicliu-pocket-linux-hardware` | Board service, hardware status/control, CPU1 clients and the nRF52840 AT host utility. |

Legacy `tdvp-camera-isp` and `tdvp-kpu-acceptance` sources remain for reference;
the product profile excludes their direct Linux camera/KPU paths. CPU1 owns
camera and AI resources. See [architecture](../docs/architecture.md) and
[AI jobs](../docs/cpu1-ai-jobs.zh-CN.md).

Applications use normal Wayland and XDG conventions. The desktop recognizes
their standard `.desktop` entries; application code does not need to link to
the session source in this directory.

The Buildroot package recipes live in
`buildroot/k230-sdk-overlay/package/` and stage these source trees during the
SDK worktree preparation step.

# Buildroot Upstream Pin

This file records the official Buildroot version used by the T-Display K230
Vision Platform.

Chinese version:

- [UPSTREAM.zh-CN.md](UPSTREAM.zh-CN.md)

## Current Baseline

The platform is pinned to:

```text
Buildroot tag: 2025.02.14
Commit:        898251ee2b83a9cd5ae0ae5db57828035a5a6f85
Series:        2025.02.x LTS
```

The submodule path is:

```text
buildroot/buildroot/
```

The upstream URL is:

```text
https://gitlab.com/buildroot.org/buildroot.git
```

## Policy

The Buildroot submodule must be pinned to an official release tag.

Rules:

- Do not track `master` for platform builds.
- Do not pin to an arbitrary development commit.
- Prefer the active LTS series for platform firmware.
- Upgrade Buildroot only through an explicit platform upgrade change.
- Keep platform customization in the outer `buildroot/` br2-external tree.
- Never patch upstream files in `buildroot/buildroot/` for platform behavior.

## Why LTS

This project is a firmware-oriented embedded vision platform. Its system base
must prioritize deterministic behavior, repeatable builds, and long-term
maintenance over new Buildroot features.

The 2025.02.x series is the current LTS baseline for this project.

## Verify The Pin

From the repository root:

```sh
git submodule status buildroot/buildroot
git -C buildroot/buildroot describe --tags --exact-match
git -C buildroot/buildroot rev-parse HEAD
```

Expected:

```text
2025.02.14
898251ee2b83a9cd5ae0ae5db57828035a5a6f85
```

## Upgrade Procedure

Buildroot upgrades are platform events. Do not upgrade implicitly.

To move to a newer official release tag:

```sh
git -C buildroot/buildroot fetch --tags
git -C buildroot/buildroot checkout <official-release-tag>
git add buildroot/buildroot
```

Then validate:

- Platform defconfig can be restored.
- `menuconfig` opens with the outer `BR2_EXTERNAL` tree.
- System image builds from a clean output directory.
- Root filesystem layout remains platform-owned.
- Kernel configuration remains minimal.
- BSP/runtime packages still integrate.
- Camera, display, input, and AI demo behavior remains stable.

The upgrade commit message must name the Buildroot release tag.

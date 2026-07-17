# Buildroot Packages

This directory contains platform-specific Buildroot packages for the T-Display
K230 Vision Platform.

It is the integration point between the Linux system image and the platform SDK
layers.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Purpose

Packages in this directory install platform components into the target root
filesystem in a deterministic way.

Allowed package categories:

- BSP package.
- Vision runtime package.
- SDK examples package.
- SDK tools package where target-side tools are required.
- Bring-up validation-only package.
- Board capability packages such as LoRa, audio, power, radio, or sensor
  support when present on the target profile.

## Package Ownership

### BSP Package

Owns installation of:

- Camera abstraction library.
- Display abstraction library.
- Input abstraction library.
- LoRa/audio/storage/power/radio abstractions when present on the target
  profile.
- Public BSP headers if target-side development is supported.

The BSP package may depend on Linux drivers being present, but its public API
must not expose Linux device details.

### Vision Runtime Package

Owns installation of:

- Pipeline engine.
- Buffer manager.
- AI inference wrapper.
- Event loop.
- Public runtime headers if target-side development is supported.

The runtime package depends on BSP APIs, not on application code.

### SDK Examples Package

Owns installation of:

- `demo_camera`.
- `demo_ai`.
- Minimal validation examples.
- Example assets required by those examples.

Examples must prove the platform API works. They must not define platform
behavior.

### Bring-Up Validation Package

Owns installation of:

- Board smoke-test tools used before the BSP API exists.
- Hardware diagnostics that validate Linux/device-tree/driver readiness.

Validation-only packages may use Linux internals directly, but only to prove
board readiness during bring-up. They must not become application examples or
public SDK APIs.

## Forbidden Packages

Do not add packages for:

- Desktop apps.
- GUI shells.
- Generic Linux tools unrelated to platform operation.
- Per-demo system dependencies.
- Application-specific services.
- Runtime package managers.
- Packages that require manual target setup after flashing.

## Package Design Rules

Each package must:

- Build from versioned source inputs.
- Install files through Buildroot package rules.
- Avoid dependence on host machine state.
- Avoid target-side downloads during normal use.
- Expose stable platform APIs only.
- Keep Linux internals below the BSP/runtime boundary.
- Be reproducible in CI.

## Expected Package Names

Recommended package names:

```text
tdvp-bsp
tdvp-runtime
tdvp-sdk-examples
tdvp-sdk-tools
```

Board capability packages should use the same prefix:

```text
tdvp-lora
tdvp-audio
```

## Review Checklist

Before adding or modifying a package, answer yes to all items:

- Is this a platform component rather than an app workaround?
- Does it install through Buildroot rules instead of overlay copying?
- Is the package deterministic?
- Can the image boot without manual setup after flashing?
- Does the package keep API boundaries intact?
- Is the dependency justified by `docs/architecture.md` or `docs/api.md`?

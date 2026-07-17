# RootFS Overlay

This directory defines the final filesystem overlay for the Buildroot-generated
root filesystem.

The overlay is a platform integration mechanism. It is not an application data
directory and not a substitute for proper Buildroot packages.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Purpose

Use the overlay only for files that must appear in the final target filesystem
and are not better expressed as package install rules.

Typical responsibilities:

- Init scripts.
- Platform default configuration.
- Runtime launcher glue.
- Static directory layout required by the platform.
- Small platform-owned files that are versioned with the firmware image.

## Expected Target Layout

The overlay may contribute files under paths such as:

```text
/etc/init.d/
/etc/vision-platform/
/opt/vision/bin/
/opt/vision/lib/
/opt/vision/models/
/opt/vision/examples/
/var/log/
/tmp/
```

Package-owned binaries and libraries should normally be installed by Buildroot
package rules, not copied by hand into the overlay.

## Boot Behavior

The default platform boot path is:

```text
BusyBox init -> platform init script -> vision runtime or configured demo
```

The init script must start platform-owned runtime behavior only. It must not
encode demo-specific business logic.

## Allowed Files

Allowed:

- `/etc/init.d/S*` scripts for platform startup.
- `/etc/vision-platform/*.conf` default configuration.
- Empty directories required by the runtime.
- Platform-owned firmware or model files only when they are versioned and
  intentionally part of the system image.

## Forbidden Files

Forbidden:

- Package manager configuration.
- Desktop configuration.
- User login customization.
- App-specific runtime data.
- Generated logs, caches, or temporary files.
- Files copied from a developer workstation.
- Per-user credentials.
- Demo-specific policy that should live in an app or runtime config.

## Determinism Rules

The overlay must produce identical filesystem output from identical source
inputs.

Do not add files whose content depends on:

- Hostname.
- Current time.
- User home directory.
- Local absolute paths.
- Developer machine state.
- Unversioned downloads.

## Review Checklist

Before adding a file to the overlay, answer yes to all items:

- Must this file exist in the target root filesystem?
- Is this file platform-owned rather than app-owned?
- Is the file too small/simple to justify a package install rule?
- Is the content deterministic?
- Does it avoid Linux internals leaking to applications?
- Is boot behavior still runtime-centered rather than demo-centered?

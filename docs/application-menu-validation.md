# Application menu integration and validation

## Package contract

The Labwc session uses wf-panel-pi's `smenu` plugin. It reads the standard XDG
menu `/etc/xdg/menus/lxde-applications.menu` through libmenu-cache. Applications
install their own `.desktop` files under `/usr/share/applications` and their
icons/resources in their package payload. User entries in
`~/.local/share/applications` are also supported.

The image supplies standard category rules: Utility, Network, AudioVideo,
Audio, Video, Graphics, Office, Development, Education, Science, Game, Settings
and System. An `OnlyUnallocated` Other menu accepts applications with unknown
or absent categories. Existing product entries retain their normal locations.
Empty groups are suppressed by the menu implementation.

For example, `Categories=Network;WebBrowser;` routes a browser to Internet.
Each feed application owns its entry and icon; it must not overwrite the
image's menu definition. Adding compatible applications to a released feed
does not require adding application names to the firmware menu.

`Hidden` and `NoDisplay` remain respected. With cache format 1.2, `TryExec` is
stored in the cache and checked by `menu_cache_app_get_is_visible()` at runtime.
The presence of an item in raw cache output alone does not prove visibility.
The panel rebuilds its popup from the current cache each time it is opened.

## Automated checks

Run before the long image build:

```sh
python3 buildroot/tools/test-tdvp-application-menu.py
sh buildroot/tools/test-tdvp-application-menu.sh
```

The Python tests check category coverage, layout reachability, fallback,
category assets, retained product entries and the actual Buildroot install
recipe. The shell test invokes the real menu-cache generator against isolated
XDG directories, checks 13 category/fallback cases, hidden entries, TryExec
metadata, and regeneration after removal. It never changes the active desktop.

Ubuntu 24.04 requires `libmenu-cache-bin`, installed by `ci-prepare-host.sh`.
The shell test accepts a menu-source directory as its first argument for board
testing, and `TDVP_MENU_CACHE_GEN` can select the generator binary explicitly.
It requests cache format 1.2, matching the device's running menu cache.

## Device acceptance — 2026-09-11

The production menu and six added category descriptors were deployed to the
device at `vicliu.i234.me:10022`. The previous menu was backed up under
`/var/lib/tdvp-validation/menu-20260911`. No compositor, panel, authentication,
renderer, camera or AI binary was replaced.

Validated on the running desktop:

- The board's real menu-cache generator passed the isolated regression.
- Temporary system-wide desktop entries appeared through the same
  libmenu-cache API used by the panel: browser in Internet, unknown category
  in Other.
- Hidden and NoDisplay fixtures were omitted; a missing TryExec fixture was
  reported hidden by `menu_cache_app_get_is_visible()`.
- The standard GTK desktop-entry launcher successfully launched the harmless
  `/bin/true` fixture in the active user's Wayland session.
- Removing the fixtures automatically removed their cache entries.
- wf-panel-pi PID 411 and Labwc PID 367 remained unchanged throughout.
- The temporary application entries were removed after testing. NetSurf was
  not installed or launched in this menu-only acceptance test.

The five host tests, including the production install recipe, and the real
generator regression also passed in an Ubuntu 24.04 container on the LAN build
host. No full image build was used for these checks.
The existing 20 opkg image-seed tests also passed there with native opkg 0.7.0,
including all optional native cases; actionlint passed for the updated workflow.
Camera-demo retirement and renderer-stack-lock regressions passed locally.
An additional Ubuntu 18.04 run hit an ext4 drift-test diagnostic mismatch with
debugfs 1.44.1; the unchanged test passed on Ubuntu 24.04. Ubuntu 24.04 remains
the release validation environment.

The running card separately retains legacy `Status: hold ok installed` package
metadata. Commit `35a521a` already fixes image generation to emit
`Status: install hold installed` and gates it with opkg tests. This menu change
does not rewrite the live package database or establish signed-feed/NetSurf
installation acceptance. The patched development card is not a byte-identical
released-image baseline; use the next verified image for release acceptance.

#!/usr/bin/env bash
set -euo pipefail
sdk_root="$(dirname "$(readlink -f "$0")")"
export PKG_CONFIG_SYSROOT_DIR="$sdk_root/sysroot"
export PKG_CONFIG_LIBDIR="$sdk_root/sysroot/usr/lib/pkgconfig:$sdk_root/sysroot/usr/share/pkgconfig"
export PKG_CONFIG_SYSTEM_INCLUDE_PATH="$sdk_root/sysroot/usr/include"
export PKG_CONFIG_SYSTEM_LIBRARY_PATH="$sdk_root/sysroot/usr/lib"
unset PKG_CONFIG_PATH
LD_LIBRARY_PATH="$sdk_root/lib" exec "$sdk_root/bin/pkgconf" --keep-system-libs "$@"

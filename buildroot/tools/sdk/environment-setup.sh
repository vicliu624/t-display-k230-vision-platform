#!/usr/bin/env bash
# Source this file in Bash. Host build tools are supplied by Ubuntu 24.04.
TDVP_SDK_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export TDVP_SDK_ROOT
export PATH="$TDVP_SDK_ROOT/bin:$PATH"
export CROSS_COMPILE="$TDVP_SDK_ROOT/bin/riscv64-unknown-linux-gnu-"
export CC="${CROSS_COMPILE}gcc" CXX="${CROSS_COMPILE}g++" CPP="${CROSS_COMPILE}cpp"
export AR="${CROSS_COMPILE}ar" AS="${CROSS_COMPILE}as" LD="${CROSS_COMPILE}ld"
export NM="${CROSS_COMPILE}nm" RANLIB="${CROSS_COMPILE}ranlib" STRIP="${CROSS_COMPILE}strip"
export OBJCOPY="${CROSS_COMPILE}objcopy" OBJDUMP="${CROSS_COMPILE}objdump" READELF="${CROSS_COMPILE}readelf"
export SDKTARGETSYSROOT="$TDVP_SDK_ROOT/sysroot"
export PKG_CONFIG="$TDVP_SDK_ROOT/bin/pkg-config"
export PKG_CONFIG_SYSROOT_DIR="$SDKTARGETSYSROOT"
export PKG_CONFIG_LIBDIR="$SDKTARGETSYSROOT/usr/lib/pkgconfig:$SDKTARGETSYSROOT/usr/share/pkgconfig"
export CFLAGS='-O2 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_FORTIFY_SOURCE=1'
export CXXFLAGS="$CFLAGS"
export CPPFLAGS=''
export LDFLAGS="-Wl,-rpath-link,$SDKTARGETSYSROOT/usr/lib"
export CONFIGURE_FLAGS='--host=riscv64-unknown-linux-gnu --prefix=/usr'
export CMAKE_TOOLCHAIN_FILE="$TDVP_SDK_ROOT/toolchain.cmake"
unset GCC_EXEC_PREFIX COMPILER_PATH LIBRARY_PATH CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH
unset PKG_CONFIG_PATH LD_RUN_PATH LD_LIBRARY_PATH
printf 'TDVP SDK: CPU0 rv64imafdc_zicsr_zifencei / lp64d, %s\n' "$TDVP_SDK_ROOT"

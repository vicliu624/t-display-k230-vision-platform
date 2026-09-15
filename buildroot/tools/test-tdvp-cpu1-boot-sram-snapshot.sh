#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
probe="$script_dir/tdvp-cpu1-boot-sram-snapshot.sh"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
mkdir "$temporary/bin"
export SNAPSHOT_CALLS="$temporary/calls" SNAPSHOT_CASE=idle
export SNAPSHOT_CAT
SNAPSHOT_CAT="$(command -v cat)"
cat > "$temporary/bin/id" <<'SH'
#!/bin/sh
[ "$SNAPSHOT_CASE" != nonroot ] && echo 0 || echo 1000
SH
cat > "$temporary/bin/uname" <<'SH'
#!/bin/sh
[ "$SNAPSHOT_CASE" != wrong_arch ] && echo riscv64 || echo x86_64
SH
cat > "$temporary/bin/cat" <<'SH'
#!/bin/sh
case "$1" in
    /sys/firmware/devicetree/base/compatible)
        case "$SNAPSHOT_CASE" in
            wrong_soc) printf 'canaan,not-k230\000' ;;
            missing_dt) exit 1 ;;
            *) printf 'canaan,canmv-k230\000canaan,kendryte-k230\000' ;;
        esac ;;
    /proc/sys/kernel/random/boot_id) echo test-boot ;;
    /proc/uptime) echo '100.0 40.0' ;;
    *) exec "$SNAPSHOT_CAT" "$@" ;;
esac
SH
cat > "$temporary/bin/devmem" <<'SH'
#!/bin/sh
set -eu
printf '%s\n' "$*" >> "$SNAPSHOT_CALLS"
[ "$#" -eq 2 ] && [ "$2" = 32 ] || exit 99
case "$1" in
    0x80800054|0x80800084|0x808000b4|0x808000e4|0x80800058|0x80808004|0x8080800c) ;;
    *) exit 99 ;;
esac
case "$SNAPSHOT_CASE:$1" in
    read_error:*) exit 1 ;;
    invalid:*) echo 'not-a-register' ;;
    nonhex:*) echo '0x0000000Z' ;;
    busy0:0x80800054|busy1:0x80800084|busy2:0x808000b4|busy3:0x808000e4) echo 0x00000001 ;;
    pause:0x80800054) echo 0x00000002 ;;
    mapped:0x80800058) echo 0x00000400 ;;
    enabled:0x80808004) echo 0x80000000 ;;
    *:0x8080800c) echo 0x00001C00 ;; # Actual board: preserve undocumented bits.
    *) echo 0x00000000 ;;
esac
SH
chmod +x "$temporary/bin/"*
export PATH="$temporary/bin:$PATH"
for SNAPSHOT_CASE in no_argument wrong_argument extra_argument nonroot wrong_arch wrong_soc missing_dt \
    idle busy0 busy1 busy2 busy3 pause mapped enabled read_error invalid nonhex; do
    : > "$SNAPSHOT_CALLS"
    args=(--read-mmio)
    case "$SNAPSHOT_CASE" in
        no_argument) args=() ;;
        wrong_argument) args=(--write-mmio) ;;
        extra_argument) args=(--read-mmio ignored) ;;
    esac
    result=0
    sh "$probe" "${args[@]}" > "$temporary/result" 2>&1 || result=$?
    case "$SNAPSHOT_CASE" in
        no_argument|wrong_argument|extra_argument|nonroot|wrong_arch|wrong_soc|missing_dt)
            [ "$result" -ne 0 ]; [ ! -s "$SNAPSHOT_CALLS" ] ;;
        read_error|invalid|nonhex)
            [ "$result" -ne 0 ]; [ "$(wc -l < "$SNAPSHOT_CALLS")" -eq 1 ]
            ! grep -q '^snapshot_no_activity=' "$temporary/result" ;;
        *)
            [ "$(wc -l < "$SNAPSHOT_CALLS")" -eq 21 ]
            for address in 0x80800054 0x80800084 0x808000b4 0x808000e4 0x80800058 0x80808004 0x8080800c; do
                [ "$(grep -Fxc "$address 32" "$SNAPSHOT_CALLS")" -eq 3 ]
            done
            grep -Fxq 'startup_order=not_proven' "$temporary/result"
            grep -Fxq 'exclusive_ownership=not_proven' "$temporary/result"
            grep -Fxq 'kpu_model_completion=not_tested' "$temporary/result"
            if [ "$SNAPSHOT_CASE" = idle ]; then
                [ "$result" -eq 0 ]; grep -Fxq 'snapshot_no_activity=1' "$temporary/result"
            else
                [ "$result" -eq 1 ]; grep -Fxq 'snapshot_no_activity=0' "$temporary/result"
            fi ;;
    esac
done
echo 'PASS 18 SRAM snapshot cases: explicit opt-in, platform checks, exact read whitelist, no KPU admission claim'

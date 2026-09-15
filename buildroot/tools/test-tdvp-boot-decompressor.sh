#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source="$project/buildroot/k230-sdk-overlay/boot/uboot/u-boot-2022.10-overlay/arch/riscv/cpu/k230/unzip.c"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
# Compile the actual production function bodies, not a replacement lifecycle.
test "$(grep -c '^static int tdvp_unzip_quiesce(' "$source")" -eq 1
test "$(grep -c '^int k230_priv_unzip(' "$source")" -eq 1
{
    grep -E '^#define (ZIP_RD_CH|ZIP_WR_CH|SDMA_CH_LENGTH)[[:space:]]' "$source"
    sed -n '/^struct ugzip_reg {/,/^};/p' "$source"
    sed -n '/^typedef struct sdma_ch_cfg {/,/^} sdma_ch_cfg_t;/p' "$source"
    sed -n '/^typedef struct gsdma_ctrl {/,/^} gsdma_ctrl_t;/p' "$source"
} > "$temporary/boot-types.inc"
{
    sed -n '/^static int tdvp_unzip_quiesce(/,/^}/p' "$source"
    sed -n '/^int k230_priv_unzip(/,/^}/p' "$source"
} > "$temporary/boot-body.inc"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -UNDEBUG -I"$temporary" \
    "$project/buildroot/tools/tests/tdvp-boot-decompressor-test.c" -o "$temporary/test"
"$temporary/test"

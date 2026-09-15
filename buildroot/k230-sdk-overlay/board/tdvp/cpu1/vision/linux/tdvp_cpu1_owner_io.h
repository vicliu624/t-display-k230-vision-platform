/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TDVP_CPU1_LINUX_OWNER_IO_H
#define TDVP_CPU1_LINUX_OWNER_IO_H
#include <linux/io.h>
#include "tdvp_vision_owner.h"

static bool tdvp_linux_owner_snapshot(const struct tdvp_owner_record __iomem *src,
                                      struct tdvp_owner_record *dst)
{
    u32 seq = readl(&src->sequence);
    unsigned int i;

    if (seq & 1U)
        return false;
    mb();
#define OWNER_READ32(field) dst->field = readl(&src->field)
#define OWNER_READ64(field) dst->field = readq(&src->field)
    OWNER_READ32(magic); OWNER_READ32(version); OWNER_READ32(bytes); OWNER_READ32(contract);
    OWNER_READ64(cookie); OWNER_READ64(peer_cookie); OWNER_READ64(heartbeat);
    OWNER_READ32(state); OWNER_READ32(fault);
    for (i = 0; i < ARRAY_SIZE(dst->reserved); ++i)
        dst->reserved[i] = readl(&src->reserved[i]);
#undef OWNER_READ32
#undef OWNER_READ64
    mb();
    dst->sequence = seq;
    return seq == readl(&src->sequence);
}

static void tdvp_linux_owner_publish(struct tdvp_owner_record __iomem *dst,
                                     const struct tdvp_owner_record *src)
{
    u32 seq = (readl(&dst->sequence) + 2U) & ~1U;
    unsigned int i;

    writel(seq - 1U, &dst->sequence);
    mb();
#define OWNER_WRITE32(field) writel(src->field, &dst->field)
#define OWNER_WRITE64(field) writeq(src->field, &dst->field)
    OWNER_WRITE32(magic); OWNER_WRITE32(version); OWNER_WRITE32(bytes); OWNER_WRITE32(contract);
    OWNER_WRITE64(cookie); OWNER_WRITE64(peer_cookie); OWNER_WRITE64(heartbeat);
    OWNER_WRITE32(state); OWNER_WRITE32(fault);
    for (i = 0; i < ARRAY_SIZE(src->reserved); ++i)
        writel(src->reserved[i], &dst->reserved[i]);
#undef OWNER_WRITE32
#undef OWNER_WRITE64
    mb();
    writel(seq, &dst->sequence);
    mb();
}
#endif

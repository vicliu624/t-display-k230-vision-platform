/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TDVP_CPU1_LINUX_OWNER_H
#define TDVP_CPU1_LINUX_OWNER_H
#include <linux/clk.h>
#include <linux/device.h>
#include "tdvp_vision_owner.h"

struct tdvp_linux_owner {
    struct tdvp_owner_control __iomem *control;
    struct tdvp_owner_session session;
    struct clk_bulk_data clocks[5];
    struct device *domains[2];
    unsigned int exclusive, powered;
    bool clocks_acquired, clocks_enabled, started;
};

int tdvp_linux_owner_prepare(struct device *, struct tdvp_linux_owner *);
/* Abort only BEFORE start. After OFFER, resources belong to the boot lifetime,
 * including all timeout/fault cases; there is no safe in-place reset protocol.
 */
void tdvp_linux_owner_abort(struct tdvp_linux_owner *);
void tdvp_linux_owner_start(struct tdvp_linux_owner *);
int tdvp_linux_owner_poll(struct tdvp_linux_owner *);
int tdvp_linux_owner_status(const struct tdvp_linux_owner *);
#endif

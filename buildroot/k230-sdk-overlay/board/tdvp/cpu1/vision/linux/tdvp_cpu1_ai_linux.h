/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TDVP_CPU1_AI_LINUX_H
#define TDVP_CPU1_AI_LINUX_H
#include <linux/device.h>
#include <linux/types.h>
struct tdvp_ai_linux;
struct tdvp_ai_linux *tdvp_ai_linux_create(struct device *parent, void __iomem *vision_control);
/* Abort only before ownership OFFER; no open or hardware request can exist. */
void tdvp_ai_linux_abort(struct tdvp_ai_linux *ai);
/* Called from the existing owner heartbeat; never takes the AI/client mutex. */
void tdvp_ai_linux_owner_update(struct tdvp_ai_linux *ai, int status, u64 owner_cookie, u64 peer_cookie);
#endif

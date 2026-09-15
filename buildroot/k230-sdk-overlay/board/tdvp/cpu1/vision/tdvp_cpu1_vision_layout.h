#ifndef TDVP_CPU1_VISION_LAYOUT_H
#define TDVP_CPU1_VISION_LAYOUT_H

/* Candidate ownership layout. The Linux DT reservation and the image guard
 * must match before this firmware may be booted. These ranges deliberately
 * do not overlap the existing CPU1 RTOS/mailbox or Linux's 512 MiB CMA pool.
 */
#define TDVP_VISION_MMZ_BASE 0x14000000UL
#define TDVP_VISION_MMZ_SIZE 0x08000000UL
#define TDVP_VISION_SHARED_BASE 0x1c000000UL
#define TDVP_VISION_SHARED_SIZE 0x02000000UL
#define TDVP_VISION_CONTROL_BASE 0x1dff0000UL
#define TDVP_VISION_CONTROL_SIZE 0x00010000UL

#endif

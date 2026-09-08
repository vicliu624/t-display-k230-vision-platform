/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_SHARED_MAP_H
#define TDVP_CPU1_SHARED_MAP_H

#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_vision_abi.h"
#include "tdvp_vision_owner.h"
#include "tdvp_ai_abi.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

/* CPU1 RT-Smart only. These reservations are not MPI-allocated MMZ blocks:
 * the pinned SDK's mmz_userdev_mmap rejects them. Its rt_dev_mem.c instead
 * supports physical mappings and explicitly selects uncached page attributes
 * when the descriptor has O_SYNC. Never use a cached alias for this protocol.
 * Keep this helper restricted to the fixed, paired-image ABI regions;
 * do not expose an arbitrary physical-memory mapping interface to Linux.
 */
static inline void *tdvp_cpu1_shared_map(uint64_t physical, size_t bytes)
{
    int protection = PROT_READ | PROT_WRITE;
    if (physical == TDVP_OWNER_BASE && bytes == TDVP_OWNER_WINDOW)
        protection = PROT_READ; /* Kernel ownership record is not ours to write. */
    else if (!((physical == TDVP_VISION_CONTROL_BASE && bytes == TDVP_VISION_CONTROL_SIZE) ||
               (physical == TDVP_VISION_SHARED_BASE &&
                bytes == TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES) ||
               ((physical == TDVP_AI_INPUT_BASE || physical == TDVP_AI_OUTPUT_BASE) &&
                bytes == TDVP_AI_BUFFER_BYTES))) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return MAP_FAILED;
    void *mapping = mmap(NULL, bytes, protection, MAP_SHARED, fd, (off_t)physical);
    int saved_errno = errno;
    (void)close(fd); /* Mapping lifetime is the worker process lifetime. */
    errno = saved_errno;
    /* RT-Smart may report a failed lwp_map_user_phy as NULL as well as -1. */
    if (!mapping) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return mapping;
}

#endif

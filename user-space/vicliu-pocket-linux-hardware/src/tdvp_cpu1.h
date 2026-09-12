#ifndef TDVP_CPU1_H
#define TDVP_CPU1_H

#include "tdvp_cpu1_abi.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tdvp_cpu1;

struct tdvp_cpu1_status {
    uint32_t features;
    uint32_t state;
    uint32_t heartbeat;
    uint32_t linux_sequence;
    uint32_t cpu1_sequence;
};

int tdvp_cpu1_open(struct tdvp_cpu1 **out);
void tdvp_cpu1_close(struct tdvp_cpu1 *cpu1);
int tdvp_cpu1_get_status(struct tdvp_cpu1 *cpu1, struct tdvp_cpu1_status *status);
int tdvp_cpu1_ping(struct tdvp_cpu1 *cpu1, uint32_t *heartbeat);
int tdvp_cpu1_crc32(struct tdvp_cpu1 *cpu1, const void *data, uint32_t size, uint32_t *crc32);

#ifdef __cplusplus
}
#endif

#endif

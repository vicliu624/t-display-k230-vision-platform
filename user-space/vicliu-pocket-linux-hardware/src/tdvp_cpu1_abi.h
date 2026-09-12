#ifndef TDVP_CPU1_ABI_H
#define TDVP_CPU1_ABI_H

#include <stdint.h>

/*
 * Shared Linux/RT-Smart wire contract.  Keep it fixed-width and append-only:
 * Linux maps this block through /dev/tdvp-cpu1 while CPU1 accesses the same
 * physical 64 KiB at 0x13ff0000 after cache maintenance.
 */
#define TDVP_CPU1_MAILBOX_PHYS 0x13ff0000UL
#define TDVP_CPU1_MAILBOX_SIZE 0x00010000UL
#define TDVP_CPU1_ABI_MAGIC 0x54445650UL /* "TDVP" */
#define TDVP_CPU1_ABI_VERSION 1U
#define TDVP_CPU1_PAYLOAD_MAX 4096U

#define TDVP_CPU1_FEATURE_PING (1U << 0)
#define TDVP_CPU1_FEATURE_CRC32 (1U << 1)

enum tdvp_cpu1_state {
    TDVP_CPU1_STATE_RESET = 0,
    TDVP_CPU1_STATE_BOOTING = 1,
    TDVP_CPU1_STATE_READY = 2,
    TDVP_CPU1_STATE_ERROR = 3,
};

enum tdvp_cpu1_command {
    TDVP_CPU1_COMMAND_NONE = 0,
    TDVP_CPU1_COMMAND_PING = 1,
    TDVP_CPU1_COMMAND_CRC32 = 2,
};

/* Both sides publish a sequence only after the preceding fields are valid. */
struct tdvp_cpu1_mailbox {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t features;
    uint32_t state;
    uint32_t heartbeat;
    uint32_t linux_sequence;
    uint32_t cpu1_sequence;
    uint32_t command;
    uint32_t payload_length;
    int32_t result;
    uint32_t result_crc32;
    uint32_t result_value;
    uint8_t payload[TDVP_CPU1_PAYLOAD_MAX];
};

#endif

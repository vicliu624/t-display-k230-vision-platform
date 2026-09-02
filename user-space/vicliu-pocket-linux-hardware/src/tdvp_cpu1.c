#define _POSIX_C_SOURCE 200809L

#include "tdvp_cpu1.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct tdvp_cpu1 {
    int fd;
    volatile struct tdvp_cpu1_mailbox *mailbox;
};

_Static_assert(sizeof(struct tdvp_cpu1_mailbox) <= TDVP_CPU1_MAILBOX_SIZE,
               "CPU1 mailbox exceeds the reserved mapping");

static void mailbox_fence(void)
{
    atomic_thread_fence(memory_order_seq_cst);
}

static int mailbox_ready(const volatile struct tdvp_cpu1_mailbox *mailbox)
{
    mailbox_fence();
    if (mailbox->magic != TDVP_CPU1_ABI_MAGIC ||
        mailbox->abi_version != TDVP_CPU1_ABI_VERSION ||
        mailbox->struct_size != sizeof(struct tdvp_cpu1_mailbox))
        return -EPROTO;
    if (mailbox->state != TDVP_CPU1_STATE_READY)
        return -EHOSTDOWN;
    return 0;
}

static int submit(struct tdvp_cpu1 *cpu1, uint32_t command, const void *payload,
                  uint32_t payload_length, uint32_t *result_value)
{
    volatile struct tdvp_cpu1_mailbox *mailbox;
    uint32_t sequence;
    unsigned int elapsed_ms;
    int status;

    if (cpu1 == NULL || payload_length > TDVP_CPU1_PAYLOAD_MAX ||
        (payload_length != 0 && payload == NULL))
        return -EINVAL;
    if (flock(cpu1->fd, LOCK_EX) != 0)
        return -errno;

    mailbox = cpu1->mailbox;
    status = mailbox_ready(mailbox);
    if (status != 0)
        goto unlock;

    sequence = mailbox->linux_sequence + 1U;
    mailbox->command = command;
    mailbox->payload_length = payload_length;
    mailbox->result = -EINPROGRESS;
    mailbox->result_crc32 = 0;
    mailbox->result_value = 0;
    if (payload_length != 0)
        memcpy((void *)mailbox->payload, payload, payload_length);
    mailbox_fence();
    mailbox->linux_sequence = sequence;
    mailbox_fence();

    for (elapsed_ms = 0; elapsed_ms < 1000U; elapsed_ms += 10U) {
        const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};

        mailbox_fence();
        if (mailbox->cpu1_sequence == sequence) {
            status = mailbox->result;
            if (status == 0 && result_value != NULL)
                *result_value = mailbox->result_value;
            goto unlock;
        }
        (void)nanosleep(&delay, NULL);
    }
    status = -ETIMEDOUT;

unlock:
    (void)flock(cpu1->fd, LOCK_UN);
    return status;
}

int tdvp_cpu1_open(struct tdvp_cpu1 **out)
{
    struct tdvp_cpu1 *cpu1;
    void *mapping;

    if (out == NULL)
        return -EINVAL;
    *out = NULL;
    cpu1 = calloc(1, sizeof(*cpu1));
    if (cpu1 == NULL)
        return -ENOMEM;
    cpu1->fd = open("/dev/tdvp-cpu1", O_RDWR | O_CLOEXEC);
    if (cpu1->fd < 0) {
        const int error = -errno;
        free(cpu1);
        return error;
    }
    mapping = mmap(NULL, TDVP_CPU1_MAILBOX_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   cpu1->fd, 0);
    if (mapping == MAP_FAILED) {
        const int error = -errno;
        (void)close(cpu1->fd);
        free(cpu1);
        return error;
    }
    cpu1->mailbox = mapping;
    *out = cpu1;
    return 0;
}

void tdvp_cpu1_close(struct tdvp_cpu1 *cpu1)
{
    if (cpu1 == NULL)
        return;
    (void)munmap((void *)cpu1->mailbox, TDVP_CPU1_MAILBOX_SIZE);
    (void)close(cpu1->fd);
    free(cpu1);
}

int tdvp_cpu1_get_status(struct tdvp_cpu1 *cpu1, struct tdvp_cpu1_status *status)
{
    if (cpu1 == NULL || status == NULL)
        return -EINVAL;
    mailbox_fence();
    status->features = cpu1->mailbox->features;
    status->state = cpu1->mailbox->state;
    status->heartbeat = cpu1->mailbox->heartbeat;
    status->linux_sequence = cpu1->mailbox->linux_sequence;
    status->cpu1_sequence = cpu1->mailbox->cpu1_sequence;
    return 0;
}

int tdvp_cpu1_ping(struct tdvp_cpu1 *cpu1, uint32_t *heartbeat)
{
    return submit(cpu1, TDVP_CPU1_COMMAND_PING, NULL, 0, heartbeat);
}

int tdvp_cpu1_crc32(struct tdvp_cpu1 *cpu1, const void *data, uint32_t size, uint32_t *crc32)
{
    uint32_t result;
    int status;

    if (size > TDVP_CPU1_PAYLOAD_MAX || (size != 0 && data == NULL))
        return -EINVAL;
    status = submit(cpu1, TDVP_CPU1_COMMAND_CRC32, data, size, &result);
    if (status != 0)
        return status;
    if (crc32 != NULL)
        *crc32 = result;
    return 0;
}

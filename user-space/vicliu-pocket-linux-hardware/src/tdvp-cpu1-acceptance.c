#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef TDVP_CPU1_TEST_EMULATED
/* Exercise the real library, replacing only the lock/wait and rejecting
 * libc copies into the device region. This does not emulate RISC-V access
 * attributes/cache coherence; the same test must also run on the board.
 */
int test_flock(int fd, int operation);
int test_nanosleep(const struct timespec *requested, struct timespec *remaining);
void *test_memcpy(void *destination, const void *source, size_t length);
#define flock test_flock
#define nanosleep test_nanosleep
#define memcpy test_memcpy
#include "tdvp_cpu1.c"
#undef memcpy
#undef nanosleep
#undef flock

static struct tdvp_cpu1_mailbox fixture;
static int forbidden_device_memcpy;
static int emulate_reply = 1;
#else
#include "tdvp_cpu1.h"
#endif

_Static_assert(offsetof(struct tdvp_cpu1_mailbox, payload) == 52,
               "Do not silently change the deployed ABI v1 layout");

static uint32_t reference_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
    }
    return ~crc;
}

#ifdef TDVP_CPU1_TEST_EMULATED
int test_flock(int fd, int operation)
{
    (void)fd;
    (void)operation;
    return 0;
}

void *test_memcpy(void *destination, const void *source, size_t length)
{
    const uintptr_t address = (uintptr_t)destination;
    if (address >= (uintptr_t)&fixture && address < (uintptr_t)(&fixture + 1))
        forbidden_device_memcpy = 1;
    /* The production call is macro-wrapped, this real copy is not. */
    return memcpy(destination, source, length);
}

int test_nanosleep(const struct timespec *requested, struct timespec *remaining)
{
    (void)requested;
    (void)remaining;
    if (emulate_reply) {
        fixture.result_value = fixture.command == TDVP_CPU1_COMMAND_PING ?
            ++fixture.heartbeat : reference_crc32(fixture.payload, fixture.payload_length);
        fixture.result = 0;
        fixture.cpu1_sequence = fixture.linux_sequence;
    }
    return 0;
}
#endif

static int check_crc(struct tdvp_cpu1 *cpu1, const void *data, uint32_t size)
{
    uint32_t actual = 0;
    const uint32_t expected = reference_crc32(data, size);
    const int error = tdvp_cpu1_crc32(cpu1, data, size, &actual);
    if (error != 0 || actual != expected) {
        fprintf(stderr, "FAIL size=%" PRIu32 " alignment=%zu error=%d "
                "actual=%08" PRIx32 " expected=%08" PRIx32 "\n",
                size, (size_t)((uintptr_t)data & 15U), error, actual, expected);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint32_t lengths[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33,
        63, 64, 65, 127, 128, 129, 255, 256, 257, 1023, 1024, 2048, 4095, 4096
    };
    _Alignas(16) uint8_t data[TDVP_CPU1_PAYLOAD_MAX + 16];
    struct tdvp_cpu1 *cpu1 = NULL;
    struct tdvp_cpu1_status before, after;
    uint32_t heartbeat;
    unsigned int checked = 0;
#ifdef TDVP_CPU1_TEST_EMULATED
    struct tdvp_cpu1 emulated = {.fd = -1, .mailbox = &fixture};
    fixture.magic = TDVP_CPU1_ABI_MAGIC;
    fixture.abi_version = TDVP_CPU1_ABI_VERSION;
    fixture.struct_size = sizeof(fixture);
    fixture.state = TDVP_CPU1_STATE_READY;
    fixture.features = TDVP_CPU1_FEATURE_PING | TDVP_CPU1_FEATURE_CRC32;
    cpu1 = &emulated;
#else
    const int open_error = tdvp_cpu1_open(&cpu1);
    if (open_error != 0) {
        fprintf(stderr, "FAIL opening CPU1: %s\n", strerror(-open_error));
        return 1;
    }
#endif
    assert(reference_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    assert(tdvp_cpu1_get_status(cpu1, &before) == 0);
    assert(before.state == TDVP_CPU1_STATE_READY);
    assert(tdvp_cpu1_ping(cpu1, &heartbeat) == 0);
    if (check_crc(cpu1, "123456789", 9) || check_crc(cpu1, NULL, 0))
        return 1;
    for (size_t index = 0; index < sizeof(data); ++index)
        data[index] = (uint8_t)(index * 37U + (index >> 8));
    for (size_t alignment = 0; alignment < 16; ++alignment) {
        for (size_t index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
            if (check_crc(cpu1, data + alignment, lengths[index]))
                return 1;
            ++checked;
        }
    }
    assert(tdvp_cpu1_crc32(cpu1, data, TDVP_CPU1_PAYLOAD_MAX + 1, &heartbeat) == -EINVAL);
    assert(tdvp_cpu1_crc32(cpu1, NULL, 1, &heartbeat) == -EINVAL);
    assert(tdvp_cpu1_crc32(NULL, data, 1, &heartbeat) == -EINVAL);
    assert(tdvp_cpu1_get_status(cpu1, &after) == 0);
    assert(after.state == TDVP_CPU1_STATE_READY);
    assert(after.linux_sequence == after.cpu1_sequence);
    assert(after.linux_sequence - before.linux_sequence == checked + 3U);
#ifdef TDVP_CPU1_TEST_EMULATED
    assert(!forbidden_device_memcpy);
    fixture.abi_version++;
    assert(tdvp_cpu1_ping(cpu1, &heartbeat) == -EPROTO);
    fixture.abi_version--;
    fixture.state = TDVP_CPU1_STATE_BOOTING;
    assert(tdvp_cpu1_ping(cpu1, &heartbeat) == -EHOSTDOWN);
    fixture.state = TDVP_CPU1_STATE_READY;
    emulate_reply = 0;
    assert(tdvp_cpu1_ping(cpu1, &heartbeat) == -ETIMEDOUT);
    printf("PASS host library regression (not a hardware/coherence test): ");
#else
    tdvp_cpu1_close(cpu1);
    printf("PASS real CPU1 mailbox: ");
#endif
    printf("%u length/alignment vectors + known CRC + empty input; "
           "sequences=%" PRIu32 "/%" PRIu32 " heartbeat=%" PRIu32 "->%" PRIu32 "\n",
           checked, after.linux_sequence, after.cpu1_sequence,
           before.heartbeat, after.heartbeat);
    return 0;
}

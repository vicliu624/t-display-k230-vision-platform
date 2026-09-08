/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int opens, maps, closes, open_result = 7;
static uint64_t expected_physical;
static size_t expected_bytes;
static int expected_protection;
static void *map_result = (void *)(uintptr_t)0x200000000ULL;

static int fake_open(const char *path, int flags, ...)
{
    ++opens;
    assert(!strcmp(path, "/dev/mem"));
    assert(flags == (O_RDWR | O_SYNC | O_CLOEXEC));
    if (open_result < 0) errno = EACCES;
    return open_result;
}

static void *fake_mmap(void *address, size_t bytes, int protection, int flags,
                       int fd, off_t offset)
{
    ++maps;
    assert(address == NULL && bytes == expected_bytes);
    assert(protection == expected_protection && flags == MAP_SHARED);
    assert(fd == 7 && (uint64_t)offset == expected_physical);
    if (map_result == MAP_FAILED) errno = ENXIO;
    return map_result;
}

static int fake_close(int fd)
{
    ++closes;
    assert(fd == 7);
    errno = EBADF; /* Closing must not obscure a mapping error. */
    return 0;
}

#define open fake_open
#define mmap fake_mmap
#define close fake_close
#include "tdvp_cpu1_shared_map.h"
#undef open
#undef mmap
#undef close

int main(void)
{
    const struct { uint64_t physical; size_t bytes; int protection; } regions[] = {
        {TDVP_OWNER_BASE, TDVP_OWNER_WINDOW, PROT_READ},
        {TDVP_VISION_CONTROL_BASE, TDVP_VISION_CONTROL_SIZE, PROT_READ | PROT_WRITE},
        {TDVP_VISION_SHARED_BASE, TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES,
         PROT_READ | PROT_WRITE},
    };
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        expected_physical = regions[i].physical;
        expected_bytes = regions[i].bytes;
        expected_protection = regions[i].protection;
        assert(!(expected_physical & 4095) && !(expected_bytes & 4095));
        assert(tdvp_cpu1_shared_map(expected_physical, expected_bytes) == map_result);
    }
    assert(opens == 3 && maps == 3 && closes == 3);
    /* Reject MMZ, MMIO, a wider shared range, misalignment and overflow. */
    const uint64_t rejected[] = {TDVP_VISION_MMZ_BASE, 0x91100000,
                                TDVP_OWNER_BASE + 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        assert(tdvp_cpu1_shared_map(rejected[i], 4096) == MAP_FAILED);
        assert(errno == EINVAL);
    }
    assert(tdvp_cpu1_shared_map(TDVP_VISION_SHARED_BASE, TDVP_VISION_SHARED_SIZE) == MAP_FAILED);
    assert(tdvp_cpu1_shared_map(TDVP_OWNER_BASE, 0) == MAP_FAILED);
    assert(opens == 3 && maps == 3 && closes == 3);

    open_result = -1;
    assert(tdvp_cpu1_shared_map(expected_physical, expected_bytes) == MAP_FAILED);
    assert(errno == EACCES && opens == 4 && maps == 3 && closes == 3);
    open_result = 7;
    map_result = MAP_FAILED;
    assert(tdvp_cpu1_shared_map(expected_physical, expected_bytes) == MAP_FAILED);
    assert(errno == ENXIO && opens == 5 && maps == 4 && closes == 4);
    map_result = NULL;
    assert(tdvp_cpu1_shared_map(expected_physical, expected_bytes) == MAP_FAILED);
    assert(errno == ENOMEM && opens == 6 && maps == 5 && closes == 5);
    puts("PASS CPU1 fixed shared mappings: O_SYNC, exact ranges, errors and descriptor cleanup");
    return 0;
}

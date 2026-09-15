#include "tdvp_cpu1.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *state_name(uint32_t state)
{
    switch (state) {
    case TDVP_CPU1_STATE_RESET:
        return "reset";
    case TDVP_CPU1_STATE_BOOTING:
        return "booting";
    case TDVP_CPU1_STATE_READY:
        return "ready";
    case TDVP_CPU1_STATE_ERROR:
        return "error";
    default:
        return "uninitialized";
    }
}

static int report_error(const char *operation, int error)
{
    fprintf(stderr, "tdvp-cpu1ctl: %s: %s\n", operation, strerror(-error));
    return 1;
}

int main(int argc, char *argv[])
{
    struct tdvp_cpu1 *cpu1 = NULL;
    struct tdvp_cpu1_status status;
    uint32_t value = 0;
    int error;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s status | ping | crc32 <text>\n", argv[0]);
        return 2;
    }
    error = tdvp_cpu1_open(&cpu1);
    if (error != 0)
        return report_error("open /dev/tdvp-cpu1", error);

    if (strcmp(argv[1], "status") == 0) {
        error = tdvp_cpu1_get_status(cpu1, &status);
        if (error == 0) {
            printf("state=%s\n", state_name(status.state));
            printf("features=0x%08" PRIx32 "\n", status.features);
            printf("heartbeat=%" PRIu32 "\n", status.heartbeat);
            printf("linux_sequence=%" PRIu32 "\n", status.linux_sequence);
            printf("cpu1_sequence=%" PRIu32 "\n", status.cpu1_sequence);
        }
    } else if (strcmp(argv[1], "ping") == 0 && argc == 2) {
        error = tdvp_cpu1_ping(cpu1, &value);
        if (error == 0)
            printf("heartbeat=%" PRIu32 "\n", value);
    } else if (strcmp(argv[1], "crc32") == 0 && argc == 3) {
        const size_t length = strlen(argv[2]);

        if (length > TDVP_CPU1_PAYLOAD_MAX)
            error = -EMSGSIZE;
        else
            error = tdvp_cpu1_crc32(cpu1, argv[2], (uint32_t)length, &value);
        if (error == 0)
            printf("crc32=%08" PRIx32 "\n", value);
    } else {
        fprintf(stderr, "Usage: %s status | ping | crc32 <text>\n", argv[0]);
        tdvp_cpu1_close(cpu1);
        return 2;
    }

    tdvp_cpu1_close(cpu1);
    return error == 0 ? 0 : report_error(argv[1], error);
}

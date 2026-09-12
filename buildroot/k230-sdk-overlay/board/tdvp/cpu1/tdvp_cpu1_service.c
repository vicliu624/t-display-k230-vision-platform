#include <rtthread.h>

#include "../c908/cache.h"
#include "tdvp_cpu1_abi.h"

#include <stdint.h>

#define TDVP_CPU1_SERVICE_STACK_SIZE 2048U
#define TDVP_CPU1_SERVICE_PRIORITY 20U
#define TDVP_CPU1_SERVICE_TICK 10U

static volatile struct tdvp_cpu1_mailbox *const tdvp_mailbox =
    (volatile struct tdvp_cpu1_mailbox *)TDVP_CPU1_MAILBOX_PHYS;

static uint32_t tdvp_crc32(const volatile uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xffffffffU;
    uint32_t index;
    uint32_t bit;

    for (index = 0; index < size; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return crc ^ 0xffffffffU;
}

static void tdvp_publish(void)
{
    rt_hw_cpu_sync();
    rt_hw_cpu_dcache_clean((void *)tdvp_mailbox, sizeof(*tdvp_mailbox));
    rt_hw_cpu_sync();
}

static void tdvp_initialise_mailbox(void)
{
    rt_memset((void *)tdvp_mailbox, 0, sizeof(*tdvp_mailbox));
    tdvp_mailbox->magic = TDVP_CPU1_ABI_MAGIC;
    tdvp_mailbox->abi_version = TDVP_CPU1_ABI_VERSION;
    tdvp_mailbox->struct_size = sizeof(*tdvp_mailbox);
    tdvp_mailbox->features = TDVP_CPU1_FEATURE_PING | TDVP_CPU1_FEATURE_CRC32;
    tdvp_mailbox->state = TDVP_CPU1_STATE_READY;
    tdvp_publish();
}

static void tdvp_cpu1_service(void *parameter)
{
    uint32_t completed_sequence = 0;

    (void)parameter;
    tdvp_initialise_mailbox();
    for (;;) {
        uint32_t sequence;
        uint32_t length;

        rt_hw_cpu_dcache_invalidate((void *)tdvp_mailbox, sizeof(*tdvp_mailbox));
        rt_hw_cpu_sync();
        tdvp_mailbox->heartbeat++;
        sequence = tdvp_mailbox->linux_sequence;
        if (sequence != completed_sequence) {
            length = tdvp_mailbox->payload_length;
            tdvp_mailbox->result = 0;
            tdvp_mailbox->result_value = 0;
            tdvp_mailbox->result_crc32 = 0;
            if (length > TDVP_CPU1_PAYLOAD_MAX) {
                tdvp_mailbox->result = -RT_EINVAL;
            } else if (tdvp_mailbox->command == TDVP_CPU1_COMMAND_PING) {
                tdvp_mailbox->result_value = tdvp_mailbox->heartbeat;
            } else if (tdvp_mailbox->command == TDVP_CPU1_COMMAND_CRC32) {
                tdvp_mailbox->result_crc32 = tdvp_crc32(tdvp_mailbox->payload, length);
                tdvp_mailbox->result_value = tdvp_mailbox->result_crc32;
            } else {
                tdvp_mailbox->result = -RT_ENOSYS;
            }
            completed_sequence = sequence;
            tdvp_mailbox->cpu1_sequence = completed_sequence;
        }
        tdvp_publish();
        rt_thread_mdelay(TDVP_CPU1_SERVICE_TICK);
    }
}

static int tdvp_cpu1_service_init(void)
{
    rt_thread_t thread = rt_thread_create("tdvp_cpu1", tdvp_cpu1_service, RT_NULL,
                                          TDVP_CPU1_SERVICE_STACK_SIZE,
                                          TDVP_CPU1_SERVICE_PRIORITY,
                                          TDVP_CPU1_SERVICE_TICK);

    if (thread == RT_NULL)
        return -RT_ENOMEM;
    rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(tdvp_cpu1_service_init);

/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <dfs_fs.h>
#include <dfs_romfs.h>
#include <sys/stat.h>

/* Generated from the pinned RT-Smart mkromfs.py and the cross-built worker.
 * Never add the SD card or CanMV filesystem startup to this mount path.
 */
#include "tdvp_vision_romfs.inc"

static int mount_status = -RT_ERROR;

int tdvp_cpu1_vision_mount_status(void)
{
    return mount_status;
}

static int tdvp_cpu1_vision_mount(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root)) {
        rt_kprintf("TDVP CPU1 vision: ROMFS mount failed\n");
        return -RT_ERROR;
    }
    if (mkdir("/dev/shm", 0777) || dfs_mount(RT_NULL, "/dev/shm", "tmp", 0, RT_NULL)) {
        rt_kprintf("TDVP CPU1 vision: shared tmpfs mount failed\n");
        return -RT_ERROR;
    }
    if (dfs_mount(RT_NULL, "/tmp", "tmp", 0, RT_NULL)) {
        rt_kprintf("TDVP CPU1 vision: tmpfs mount failed\n");
        return -RT_ERROR;
    }
    mount_status = RT_EOK;
    return mount_status;
}
INIT_ENV_EXPORT(tdvp_cpu1_vision_mount);

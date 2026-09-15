/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <lwp.h>

extern int tdvp_cpu1_vision_mount_status(void);

/* Called by main after INIT_ENV_EXPORT has mounted the embedded ROMFS.
 * The worker uses RT-Smart MPI ioctls, not Linux syscalls or an SD executable.
 */
int tdvp_cpu1_vision_launch(void)
{
    char *argv[] = {"/bin/tdvp-vision-worker.elf", RT_NULL};
    char *envp[] = {"PATH=/bin", "HOME=/", RT_NULL};
    int pid;

    if (tdvp_cpu1_vision_mount_status()) {
        rt_kprintf("TDVP CPU1 vision: refusing launch after filesystem initialization failure\n");
        return -RT_ERROR;
    }
    pid = lwp_execve(argv[0], 0, 1, argv, envp);

    if (pid <= 0) {
        rt_kprintf("TDVP CPU1 vision: worker launch failed: %d\n", pid);
        return pid < 0 ? pid : -RT_ERROR;
    }
    rt_kprintf("TDVP CPU1 vision: worker pid=%d (capture readiness is asynchronous)\n", pid);
    return RT_EOK;
}

/* The mailbox service starts through INIT_APP_EXPORT. CPU1 must not mount,
 * resize or format the Linux SD card, or run CanMV's USB/network startup.
 */
#include <rtthread.h>

#ifdef RT_USING_MPP
extern int tdvp_cpu1_vision_init_status(void);
extern int tdvp_cpu1_vision_launch(void);
#endif

int main(void)
{
#ifdef RT_USING_MPP
    /* MPP readiness is distinct from mailbox readiness. Never imply that
     * registering the media drivers is already successful frame capture.
     */
    int status = tdvp_cpu1_vision_init_status();
    rt_kprintf("TDVP CPU1 vision driver status: %d\n", status);
    return status ? status : tdvp_cpu1_vision_launch();
#else
    return 0;
#endif
}

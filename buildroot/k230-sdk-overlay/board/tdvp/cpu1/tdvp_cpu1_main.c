/* The mailbox service starts through INIT_APP_EXPORT. CPU1 must not mount,
 * resize or format the Linux SD card, or run CanMV's USB/network startup.
 */
#include <rtthread.h>

#if defined(RT_USING_MPP) && !defined(RT_USING_TDVP_CPU1_VISION)
#error "TDVP media requires the paired vision ownership startup gate"
#endif
#ifdef RT_USING_TDVP_CPU1_VISION
extern int tdvp_cpu1_vision_startup(void);
#endif

int main(void)
{
#ifdef RT_USING_TDVP_CPU1_VISION
    return tdvp_cpu1_vision_startup();
#else
    return 0;
#endif
}

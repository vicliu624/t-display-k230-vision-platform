#include <stddef.h>
#include <vvcam_sensor.h>

#include "tdvp-vvcam-legacy-adapter.h"

extern struct vvcam_sensor vvcam_gc2093;

_Static_assert(sizeof(void *) == 8, "the scalar ISP requires LP64");
_Static_assert(sizeof(struct tdvp_vvcam_legacy_sensor) == 80,
               "legacy sensor registration ABI drift");
_Static_assert(offsetof(struct tdvp_vvcam_legacy_sensor, ctrl.set_analog_gain) == 56,
               "legacy analog gain callback ABI drift");
_Static_assert(offsetof(struct tdvp_vvcam_legacy_sensor, ctrl.set_int_time) == 72,
               "legacy exposure callback ABI drift");
_Static_assert(sizeof(struct vvcam_sensor) == 112,
               "review adapter for changed upstream sensor layout");
_Static_assert(offsetof(struct vvcam_sensor, ctrl.set_analog_gain) == 88,
               "review adapter for changed upstream gain callbacks");

/* Compile this with the board's GC2093 driver, not vendor src/lib.c. Only
 * register this board's sensor. No I2C operation, sensor initialization or
 * streaming is performed merely by loading/registering the plugin.
 * Flip callbacks are intentionally absent from the legacy interface.
 */
void vvcam_sensor_init(void)
{
    static struct tdvp_vvcam_legacy_sensor sensor;
    sensor.name = vvcam_gc2093.name;
    sensor.ctrl.init = vvcam_gc2093.ctrl.init;
    sensor.ctrl.deinit = vvcam_gc2093.ctrl.deinit;
    sensor.ctrl.enum_mode = vvcam_gc2093.ctrl.enum_mode;
    sensor.ctrl.get_mode = vvcam_gc2093.ctrl.get_mode;
    sensor.ctrl.set_mode = vvcam_gc2093.ctrl.set_mode;
    sensor.ctrl.set_stream = vvcam_gc2093.ctrl.set_stream;
    sensor.ctrl.set_analog_gain = vvcam_gc2093.ctrl.set_analog_gain;
    sensor.ctrl.set_digital_gain = vvcam_gc2093.ctrl.set_digital_gain;
    sensor.ctrl.set_int_time = vvcam_gc2093.ctrl.set_int_time;
    tdvp_vvcam_legacy_add(&sensor);
}

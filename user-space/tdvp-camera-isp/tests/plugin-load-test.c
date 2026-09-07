/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vvcam_sensor.h>
#include "tdvp-vvcam-legacy-adapter.h"

static struct tdvp_vvcam_legacy_sensor *registered;

void tdvp_vvcam_legacy_add(struct tdvp_vvcam_legacy_sensor *sensor)
{
    assert(registered == NULL);
    registered = sensor;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    void *plugin = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (plugin == NULL) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    const uint64_t *version = dlsym(plugin, "vvcam_api_version");
    assert(version && *version == 1);
    void (*register_sensor)(void) = dlsym(plugin, "vvcam_sensor_init");
    assert(register_sensor && !registered);
    register_sensor();
    assert(registered && strcmp(registered->name, "gc2093") == 0);
    void *context = NULL;
    assert(registered->ctrl.init(&context) == 0 && context != NULL);
    struct vvcam_sensor_mode mode;
    assert(registered->ctrl.enum_mode(context, 0, &mode) == 0);
    assert(mode.width == 1920 && mode.height == 1080 && mode.ae_info.cur_fps == 30);
    assert(mode.clk == 0); /* The legacy ISP must not write sensor clocks. */
    assert(registered->ctrl.enum_mode(context, 1, &mode) == 0);
    assert(mode.width == 1920 && mode.height == 1080 && mode.ae_info.cur_fps == 60);
    assert(mode.clk == 0);
    assert(registered->ctrl.enum_mode(context, 2, &mode) != 0);
    registered->ctrl.deinit(context);
    registered = NULL;
    assert(dlclose(plugin) == 0);
    puts("GC2093 plugin: PASS dynamic load, API 1, legacy registration, kernel-owned clocks for both modes (no sensor I/O)");
    return 0;
}

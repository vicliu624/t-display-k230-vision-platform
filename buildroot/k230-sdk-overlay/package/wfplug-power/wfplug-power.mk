################################################################################
#
# wfplug-power
#
################################################################################

WFPLUG_POWER_VERSION = 9f34c204706cc041cc4eacab6ad6842c6c264e6b
WFPLUG_POWER_SITE = https://github.com/raspberrypi-ui/pplug-power.git
WFPLUG_POWER_SITE_METHOD = git
WFPLUG_POWER_LICENSE = BSD-3-Clause
WFPLUG_POWER_DEPENDENCIES = gtkmm3 systemd wf-panel-pi

$(eval $(meson-package))

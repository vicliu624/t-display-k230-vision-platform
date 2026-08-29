################################################################################
#
# wfplug-batt
#
################################################################################

WFPLUG_BATT_VERSION = f4c18fbca9e1b752e35b6ea8a854676b4777de3b
WFPLUG_BATT_SITE = https://github.com/raspberrypi-ui/pplug-batt.git
WFPLUG_BATT_SITE_METHOD = git
WFPLUG_BATT_LICENSE = BSD-3-Clause
WFPLUG_BATT_DEPENDENCIES = gtkmm3 wf-panel-pi

$(eval $(meson-package))

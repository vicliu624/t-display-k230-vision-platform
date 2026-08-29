################################################################################
#
# wfplug-clock
#
################################################################################

WFPLUG_CLOCK_VERSION = 2f80788ff2f2d50ae3a43d0e80331f39abaf9ca5
WFPLUG_CLOCK_SITE = https://github.com/raspberrypi-ui/pplug-clock.git
WFPLUG_CLOCK_SITE_METHOD = git
WFPLUG_CLOCK_LICENSE = BSD-3-Clause
WFPLUG_CLOCK_DEPENDENCIES = gtkmm3 wf-panel-pi

$(eval $(meson-package))

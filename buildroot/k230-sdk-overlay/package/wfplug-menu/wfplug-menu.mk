################################################################################
#
# wfplug-menu
#
################################################################################

WFPLUG_MENU_VERSION = 20427ec79e67aba0dc36c627fa2ec5eba864cdd2
WFPLUG_MENU_SITE = https://github.com/raspberrypi-ui/pplug-menu.git
WFPLUG_MENU_SITE_METHOD = git
WFPLUG_MENU_LICENSE = BSD-3-Clause
WFPLUG_MENU_DEPENDENCIES = gtkmm3 libmenu-cache wf-panel-pi

$(eval $(meson-package))

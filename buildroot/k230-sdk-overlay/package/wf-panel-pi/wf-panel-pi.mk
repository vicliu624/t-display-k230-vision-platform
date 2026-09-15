################################################################################
#
# wf-panel-pi
#
################################################################################

WF_PANEL_PI_VERSION = 1b6010c02fc5462859994e306b0059ac4c7b1acd
WF_PANEL_PI_SITE = https://github.com/raspberrypi-ui/wf-panel-pi.git
WF_PANEL_PI_SITE_METHOD = git
WF_PANEL_PI_LICENSE = BSD-3-Clause
WF_PANEL_PI_INSTALL_STAGING = YES
WF_PANEL_PI_DEPENDENCIES = \
	glm \
	gtk-layer-shell \
	gtkmm3 \
	libevdev \
	libinput \
	libmenu-cache \
	libxml2 \
	wayland-protocols
$(eval $(meson-package))

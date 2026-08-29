################################################################################
#
# wfplug-netman
#
################################################################################

WFPLUG_NETMAN_VERSION = 9064d7b4f9c3c841bfd195dae9e0bf8110412d0f
WFPLUG_NETMAN_SITE = https://github.com/raspberrypi-ui/pplug-netman.git
WFPLUG_NETMAN_SITE_METHOD = git
WFPLUG_NETMAN_LICENSE = BSD-3-Clause
WFPLUG_NETMAN_DEPENDENCIES = gtkmm3 libnma libsecret network-manager util-linux wf-panel-pi

$(eval $(meson-package))

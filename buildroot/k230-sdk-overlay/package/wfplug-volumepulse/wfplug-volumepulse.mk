################################################################################
#
# wfplug-volumepulse
#
################################################################################

WFPLUG_VOLUMEPULSE_VERSION = bb046d5bbc95789ca7909f1473726e8eacd1d6f6
WFPLUG_VOLUMEPULSE_SITE = https://github.com/raspberrypi-ui/pplug-volumepulse.git
WFPLUG_VOLUMEPULSE_SITE_METHOD = git
WFPLUG_VOLUMEPULSE_LICENSE = BSD-3-Clause
WFPLUG_VOLUMEPULSE_DEPENDENCIES = gtkmm3 libcanberra pulseaudio wf-panel-pi

$(eval $(meson-package))

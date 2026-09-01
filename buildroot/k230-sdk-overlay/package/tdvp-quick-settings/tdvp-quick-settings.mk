################################################################################
#
# tdvp-quick-settings
#
################################################################################

# Keep this version in lockstep with the immutable tag in the independent
# tdvp-quick-settings repository. The firmware owns only this Buildroot bridge;
# application source remains outside this repository.
TDVP_QUICK_SETTINGS_VERSION = 0.1.8
TDVP_QUICK_SETTINGS_SITE = $(call github,vicliu624,tdvp-quick-settings,v$(TDVP_QUICK_SETTINGS_VERSION))
TDVP_QUICK_SETTINGS_LICENSE = MIT
TDVP_QUICK_SETTINGS_LICENSE_FILES = LICENSE
TDVP_QUICK_SETTINGS_DEPENDENCIES = cairo wayland wayland-protocols host-wayland
TDVP_QUICK_SETTINGS_CONF_OPTS = \
	-DTDVP_QS_BUILD_WAYLAND=ON \
	-DTDVP_QS_INSTALL_K230_AUTOSTART=OFF \
	-DBUILD_TESTING=OFF \
	-DTDVP_QS_WARNINGS_AS_ERRORS=ON

$(eval $(cmake-package))

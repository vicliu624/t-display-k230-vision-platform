################################################################################
#
# labwc
#
################################################################################

LABWC_VERSION = 9af441ecd36bbee66d4df46baa7b482872d989f2
LABWC_SITE = https://github.com/labwc/labwc.git
LABWC_SITE_METHOD = git
LABWC_LICENSE = GPL-2.0-only
LABWC_LICENSE_FILES = LICENSE

LABWC_DEPENDENCIES = \
	host-pkgconf \
	host-wayland \
	cairo \
	libglib2 \
	libinput \
	libpng \
	libxml2 \
	pango \
	pixman \
	wayland \
	wayland-protocols \
	wlroots

LABWC_CONF_OPTS = \
	-Dicon=disabled \
	-Dnls=disabled \
	-Dsvg=disabled \
	-Dtest=disabled \
	-Dxwayland=disabled

$(eval $(meson-package))

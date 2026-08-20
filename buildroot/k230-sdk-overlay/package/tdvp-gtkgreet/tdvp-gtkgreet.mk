################################################################################
#
# gtkgreet
#
################################################################################

TDVP_GTKGREET_VERSION = 0.8
TDVP_GTKGREET_SITE = https://git.sr.ht/~kennylevinsen/gtkgreet/archive
TDVP_GTKGREET_SOURCE = $(TDVP_GTKGREET_VERSION).tar.gz
TDVP_GTKGREET_LICENSE = GPL-3.0-only
TDVP_GTKGREET_LICENSE_FILES = LICENSE
TDVP_GTKGREET_DEPENDENCIES = host-pkgconf json-c libgtk3 gtk-layer-shell
TDVP_GTKGREET_CONF_OPTS = \
	-Dlayershell=enabled \
	-Dman-pages=disabled

$(eval $(meson-package))

################################################################################
#
# libnma
#
################################################################################

LIBNMA_VERSION = 1.10.6
LIBNMA_SOURCE = libnma-$(LIBNMA_VERSION).tar.xz
LIBNMA_SITE = https://download.gnome.org/sources/libnma/1.10
LIBNMA_INSTALL_STAGING = YES
LIBNMA_LICENSE = GPL-2.0+
LIBNMA_LICENSE_FILES = COPYING
LIBNMA_DEPENDENCIES = libgtk3 libsecret network-manager
LIBNMA_CONF_OPTS = \
	-Dgcr=false \
	-Dgtk_doc=false \
	-Dintrospection=false \
	-Diso_codes=false \
	-Dld_gc=false \
	-Dlibnma_gtk4=false \
	-Dmobile_broadband_provider_info=false \
	-Dvapi=false

$(eval $(meson-package))

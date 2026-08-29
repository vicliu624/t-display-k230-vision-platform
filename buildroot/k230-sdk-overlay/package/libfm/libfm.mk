################################################################################
#
# libfm
#
################################################################################

LIBFM_VERSION = 2f1f90db66c121a57f915f48fb89a5bb515d3d16
LIBFM_SITE = https://github.com/raspberrypi-ui/libfm.git
LIBFM_SITE_METHOD = git
LIBFM_LICENSE = GPL-2.0+
LIBFM_LICENSE_FILES = COPYING
LIBFM_INSTALL_STAGING = YES
LIBFM_AUTORECONF = YES
LIBFM_DEPENDENCIES = host-intltool libglib2 libgtk3 libmenu-cache
LIBFM_CONF_OPTS = \
	--with-gtk=3 \
	--disable-udisks \
	--disable-exif \
	--disable-old-actions \
	--disable-gtk-doc

$(eval $(autotools-package))

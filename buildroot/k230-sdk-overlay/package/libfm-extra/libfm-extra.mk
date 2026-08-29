################################################################################
#
# libfm-extra
#
################################################################################

LIBFM_EXTRA_VERSION = 2f1f90db66c121a57f915f48fb89a5bb515d3d16
LIBFM_EXTRA_SITE = https://github.com/raspberrypi-ui/libfm.git
LIBFM_EXTRA_SITE_METHOD = git
LIBFM_EXTRA_LICENSE = GPL-2.0+
LIBFM_EXTRA_LICENSE_FILES = COPYING
LIBFM_EXTRA_INSTALL_STAGING = YES
LIBFM_EXTRA_AUTORECONF = YES
LIBFM_EXTRA_DEPENDENCIES = host-intltool libglib2
LIBFM_EXTRA_CONF_OPTS = \
	--with-extra-only=yes \
	--disable-old-actions \
	--disable-gtk-doc

$(eval $(autotools-package))

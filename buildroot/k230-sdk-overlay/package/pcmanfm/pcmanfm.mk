################################################################################
#
# pcmanfm
#
################################################################################

PCMANFM_VERSION = db37080c1820388c872d6784e675c845b791f073
PCMANFM_SITE = https://github.com/raspberrypi-ui/pcmanfm.git
PCMANFM_SITE_METHOD = git
PCMANFM_LICENSE = GPL-2.0+
PCMANFM_LICENSE_FILES = COPYING
PCMANFM_AUTORECONF = YES
PCMANFM_DEPENDENCIES = \
	host-intltool \
	gtk-layer-shell \
	libfm \
	libgtk3
PCMANFM_CONF_OPTS = --with-gtk=3

$(eval $(autotools-package))

################################################################################
#
# libmenu-cache
#
################################################################################

LIBMENU_CACHE_VERSION = cd6f68470a86a6381d45680a71b95c30876180a9
LIBMENU_CACHE_SITE = https://github.com/lxde/menu-cache.git
LIBMENU_CACHE_SITE_METHOD = git
LIBMENU_CACHE_LICENSE = LGPL-2.1+
LIBMENU_CACHE_LICENSE_FILES = COPYING
LIBMENU_CACHE_INSTALL_STAGING = YES
LIBMENU_CACHE_AUTORECONF = YES
LIBMENU_CACHE_DEPENDENCIES = host-intltool libfm-extra libglib2
LIBMENU_CACHE_CONF_OPTS = --disable-gtk-doc

$(eval $(autotools-package))

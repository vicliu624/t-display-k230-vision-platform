################################################################################
#
# gtk-session-lock (0.2.0, pinned upstream commit)
#
################################################################################
GTK_SESSION_LOCK_VERSION = b3544f361498d716b1ceef1ad6ac9bdf024bf782
GTK_SESSION_LOCK_SITE = https://github.com/Cu3PO42/gtk-session-lock.git
GTK_SESSION_LOCK_SITE_METHOD = git
GTK_SESSION_LOCK_LICENSE = GPL-3.0-only, MIT
GTK_SESSION_LOCK_LICENSE_FILES = LICENSE_GPL.txt LICENSE_MIT.txt
GTK_SESSION_LOCK_INSTALL_STAGING = YES
GTK_SESSION_LOCK_DEPENDENCIES = host-pkgconf host-wayland libgtk3 wayland wayland-protocols
GTK_SESSION_LOCK_CONF_OPTS = \
	-Ddocs=false -Dexamples=false -Dintrospection=false -Dtests=false -Dvapi=false

$(eval $(meson-package))

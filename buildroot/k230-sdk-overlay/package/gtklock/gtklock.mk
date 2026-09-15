################################################################################
#
# gtklock (4.0.0, pinned upstream commit)
#
################################################################################
GTKLOCK_VERSION = 66321fb2bf0d5869d779e7ac6b4d8d9c272ea707
GTKLOCK_SITE = https://github.com/jovanlanik/gtklock.git
GTKLOCK_SITE_METHOD = git
GTKLOCK_LICENSE = GPL-3.0-only
GTKLOCK_LICENSE_FILES = LICENSE
GTKLOCK_DEPENDENCIES = host-pkgconf host-gettext gtk-session-lock libgtk3 linux-pam
GTKLOCK_CONF_OPTS = -Dman-pages=disabled

# Paired with 0001: auth runs once in a worker, with a terminal result only.
# The regression compiles this exact file against upstream auth.h + PAM headers.
define GTKLOCK_INSTALL_AUTH_BACKEND
	$(INSTALL) -m 0644 $(GTKLOCK_PKGDIR)/src/tdvp-auth.c $(@D)/src/auth.c
endef
GTKLOCK_POST_PATCH_HOOKS += GTKLOCK_INSTALL_AUTH_BACKEND

define GTKLOCK_INSTALL_TDVP_CONFIG
	$(INSTALL) -D -m 0644 $(GTKLOCK_PKGDIR)/src/gtklock $(TARGET_DIR)/etc/pam.d/gtklock
	$(INSTALL) -D -m 0644 $(GTKLOCK_PKGDIR)/src/config.ini $(TARGET_DIR)/etc/gtklock/config.ini
	$(INSTALL) -D -m 0644 $(GTKLOCK_PKGDIR)/src/style.css $(TARGET_DIR)/etc/gtklock/style.css
endef
GTKLOCK_POST_INSTALL_TARGET_HOOKS += GTKLOCK_INSTALL_TDVP_CONFIG

# Only the PAM shadow-check helper is privileged, never this GTK application.
define GTKLOCK_PERMISSIONS
	/usr/sbin/unix_chkpwd f 4755 0 0 - - - - -
endef

$(eval $(meson-package))

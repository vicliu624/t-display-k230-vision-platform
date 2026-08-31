################################################################################
#
# swaylock
#
################################################################################

SWAYLOCK_VERSION = 1.8.1
SWAYLOCK_SITE = $(call github,swaywm,swaylock,v$(SWAYLOCK_VERSION))
SWAYLOCK_LICENSE = MIT
SWAYLOCK_LICENSE_FILES = LICENSE
SWAYLOCK_DEPENDENCIES = \
	host-pkgconf \
	cairo \
	libxkbcommon \
	linux-pam \
	wayland \
	wayland-protocols
SWAYLOCK_CONF_OPTS = \
	-Dpam=enabled \
	-Dgdk-pixbuf=disabled \
	-Dman-pages=disabled \
	-Dzsh-completions=false \
	-Dbash-completions=false \
	-Dfish-completions=false

define SWAYLOCK_INSTALL_TDVP_PAM_SERVICE
	$(INSTALL) -D -m 0644 $(SWAYLOCK_PKGDIR)/src/swaylock \
		$(TARGET_DIR)/etc/pam.d/swaylock
endef

SWAYLOCK_POST_INSTALL_TARGET_HOOKS += SWAYLOCK_INSTALL_TDVP_PAM_SERVICE

$(eval $(meson-package))

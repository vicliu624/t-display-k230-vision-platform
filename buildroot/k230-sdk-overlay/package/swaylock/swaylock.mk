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

# pam_unix delegates unprivileged password checks to this narrowly scoped
# helper. Buildroot's linux-pam package installs it without setuid metadata;
# an ordinary Wayland session otherwise cannot read root-only /etc/shadow.
# Apply ownership/mode during fakeroot, not a privileged host install hook.
# Do not make swaylock itself setuid and do not relax shadow permissions.
define SWAYLOCK_PERMISSIONS
	/usr/sbin/unix_chkpwd f 4755 0 0 - - - - -
endef

$(eval $(meson-package))

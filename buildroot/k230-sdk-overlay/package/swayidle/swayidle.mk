################################################################################
#
# swayidle
#
################################################################################

# 1.8.0 uses the ext-idle-notify protocol generation shipped by TDVP's
# Wayland-protocols 1.39 baseline. Keep it aligned with the already-pinned
# swaylock 1.8.x series rather than making the desktop require a newer
# Wayland-protocols ABI.
SWAYIDLE_VERSION = 1.8.0
SWAYIDLE_SITE = $(call github,swaywm,swayidle,$(SWAYIDLE_VERSION))
SWAYIDLE_LICENSE = MIT
SWAYIDLE_LICENSE_FILES = LICENSE
SWAYIDLE_DEPENDENCIES = host-pkgconf host-wayland systemd wayland wayland-protocols
SWAYIDLE_CONF_OPTS = \
	-Dlogind=enabled \
	-Dlogind-provider=systemd \
	-Dman-pages=disabled \
	-Dbash-completions=false \
	-Dfish-completions=false \
	-Dzsh-completions=false

$(eval $(meson-package))

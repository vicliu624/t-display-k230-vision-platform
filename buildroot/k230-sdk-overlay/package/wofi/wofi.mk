################################################################################
#
# wofi
#
################################################################################

WOFI_VERSION = v1.4.1
WOFI_SITE = $(call github,SimplyCEO,wofi,$(WOFI_VERSION))
WOFI_LICENSE = GPL-3.0+
WOFI_LICENSE_FILES = LICENSE
WOFI_DEPENDENCIES = host-pkgconf libgtk3 libxkbcommon wayland wayland-protocols
$(eval $(meson-package))

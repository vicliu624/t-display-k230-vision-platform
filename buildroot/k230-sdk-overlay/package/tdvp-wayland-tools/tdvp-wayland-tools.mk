################################################################################
#
# TDVP Wayland acceptance tools
#
################################################################################

TDVP_WAYLAND_TOOLS_SITE = $(TOPDIR)/package/tdvp-wayland-tools/src
TDVP_WAYLAND_TOOLS_SITE_METHOD = local
TDVP_WAYLAND_TOOLS_LICENSE = MIT
TDVP_WAYLAND_TOOLS_LICENSE_FILES = LICENSE
TDVP_WAYLAND_TOOLS_DEPENDENCIES = host-wayland wayland wayland-protocols wlroots

# The VGLite fork uses an immutable commit as WLROOTS_VERSION, not 0.18.2.
TDVP_WAYLAND_TOOLS_PROTOCOL = $(BUILD_DIR)/wlroots-$(WLROOTS_VERSION)/protocol/wlr-screencopy-unstable-v1.xml

define TDVP_WAYLAND_TOOLS_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(TDVP_WAYLAND_TOOLS_PROTOCOL) \
		$(@D)/wlr-screencopy-unstable-v1-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(TDVP_WAYLAND_TOOLS_PROTOCOL) \
		$(@D)/wlr-screencopy-unstable-v1-protocol.c
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror \
		-I$(@D) $(@D)/tdvp-wayland-screenshot.c \
		$(@D)/wlr-screencopy-unstable-v1-protocol.c \
		-o $(@D)/tdvp-wayland-screenshot \
		$(TARGET_LDFLAGS) -lwayland-client
endef

define TDVP_WAYLAND_TOOLS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wayland-screenshot \
		$(TARGET_DIR)/usr/bin/tdvp-wayland-screenshot
	$(INSTALL) -D -m 0644 $(@D)/LICENSE \
		$(TARGET_DIR)/usr/share/doc/tdvp-wayland-tools/LICENSE
endef

$(eval $(generic-package))

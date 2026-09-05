################################################################################
#
# tdvp-wayland-acceptance
#
################################################################################

TDVP_WAYLAND_ACCEPTANCE_SITE = $(TOPDIR)/package/tdvp-wayland-acceptance/src
TDVP_WAYLAND_ACCEPTANCE_SITE_METHOD = local
TDVP_WAYLAND_ACCEPTANCE_DEPENDENCIES = libxkbcommon wayland wayland-protocols

define TDVP_WAYLAND_ACCEPTANCE_BUILD_CMDS
	$(HOST_DIR)/bin/wayland-scanner client-header \
		$(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
		$(@D)/xdg-shell-client-protocol.h
	$(HOST_DIR)/bin/wayland-scanner private-code \
		$(STAGING_DIR)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
		$(@D)/xdg-shell-protocol.c
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -O2 \
		$(@D)/tdvp-wayland-acceptance.c \
		-o $(@D)/tdvp-wayland-acceptance \
		$(TARGET_LDFLAGS) -lwayland-client -lxkbcommon
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -O2 -I$(@D) \
		$(@D)/tdvp-wayland-shm-bench.c $(@D)/xdg-shell-protocol.c \
		-o $(@D)/tdvp-wayland-shm-bench \
		$(TARGET_LDFLAGS) -lwayland-client
endef

define TDVP_WAYLAND_ACCEPTANCE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wayland-acceptance \
		$(TARGET_DIR)/usr/bin/tdvp-wayland-acceptance
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wayland-shm-bench \
		$(TARGET_DIR)/usr/bin/tdvp-wayland-shm-bench
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wayland-shm-bench-session \
		$(TARGET_DIR)/usr/bin/tdvp-wayland-shm-bench-session
endef

$(eval $(generic-package))

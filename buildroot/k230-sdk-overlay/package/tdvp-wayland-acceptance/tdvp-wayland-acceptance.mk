################################################################################
#
# tdvp-wayland-acceptance
#
################################################################################

TDVP_WAYLAND_ACCEPTANCE_SITE = $(TOPDIR)/package/tdvp-wayland-acceptance/src
TDVP_WAYLAND_ACCEPTANCE_SITE_METHOD = local
TDVP_WAYLAND_ACCEPTANCE_DEPENDENCIES = libxkbcommon wayland

define TDVP_WAYLAND_ACCEPTANCE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -O2 \
		$(@D)/tdvp-wayland-acceptance.c \
		-o $(@D)/tdvp-wayland-acceptance \
		$(TARGET_LDFLAGS) -lwayland-client -lxkbcommon
endef

define TDVP_WAYLAND_ACCEPTANCE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wayland-acceptance \
		$(TARGET_DIR)/usr/bin/tdvp-wayland-acceptance
endef

$(eval $(generic-package))

################################################################################
#
# tdvp-display-smoke
#
################################################################################

TDVP_DISPLAY_SMOKE_SITE = $(BR2_EXTERNAL_TDVP_PATH)/package/tdvp-display-smoke/src
TDVP_DISPLAY_SMOKE_SITE_METHOD = local
TDVP_DISPLAY_SMOKE_DEPENDENCIES = libdrm

define TDVP_DISPLAY_SMOKE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -O2 \
		-I$(STAGING_DIR)/usr/include/libdrm \
		$(@D)/tdvp-display-smoke.c \
		-o $(@D)/tdvp-display-smoke \
		$(TARGET_LDFLAGS) -ldrm
endef

define TDVP_DISPLAY_SMOKE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-display-smoke \
		$(TARGET_DIR)/usr/bin/tdvp-display-smoke
endef

$(eval $(generic-package))

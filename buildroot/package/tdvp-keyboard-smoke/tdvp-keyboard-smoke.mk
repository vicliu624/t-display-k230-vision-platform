################################################################################
#
# tdvp-keyboard-smoke
#
################################################################################

TDVP_KEYBOARD_SMOKE_SITE = $(BR2_EXTERNAL_TDVP_PATH)/package/tdvp-keyboard-smoke/src
TDVP_KEYBOARD_SMOKE_SITE_METHOD = local
TDVP_KEYBOARD_SMOKE_DEPENDENCIES = libgpiod

define TDVP_KEYBOARD_SMOKE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -O2 \
		$(@D)/tdvp-keyboard-smoke.c \
		-o $(@D)/tdvp-keyboard-smoke \
		$(TARGET_LDFLAGS) -lgpiod
endef

define TDVP_KEYBOARD_SMOKE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-keyboard-smoke \
		$(TARGET_DIR)/usr/bin/tdvp-keyboard-smoke
endef

$(eval $(generic-package))

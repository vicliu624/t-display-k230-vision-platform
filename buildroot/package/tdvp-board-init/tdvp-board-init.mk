################################################################################
#
# tdvp-board-init
#
################################################################################

TDVP_BOARD_INIT_SITE = $(BR2_EXTERNAL_TDVP_PATH)/package/tdvp-board-init/src
TDVP_BOARD_INIT_SITE_METHOD = local

define TDVP_BOARD_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/S20tdvp-modules \
		$(TARGET_DIR)/etc/init.d/S20tdvp-modules
endef

$(eval $(generic-package))

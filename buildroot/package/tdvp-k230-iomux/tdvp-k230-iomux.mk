################################################################################
#
# tdvp-k230-iomux
#
################################################################################

TDVP_K230_IOMUX_SITE = $(BR2_EXTERNAL_TDVP_PATH)/package/tdvp-k230-iomux/src
TDVP_K230_IOMUX_SITE_METHOD = local

define TDVP_K230_IOMUX_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -O2 \
		$(@D)/tdvp-k230-iomux.c \
		-o $(@D)/tdvp-k230-iomux \
		$(TARGET_LDFLAGS)
endef

define TDVP_K230_IOMUX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-k230-iomux \
		$(TARGET_DIR)/usr/bin/tdvp-k230-iomux
endef

$(eval $(generic-package))

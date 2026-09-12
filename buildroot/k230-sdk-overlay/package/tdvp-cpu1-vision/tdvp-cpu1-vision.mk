################################################################################
# CPU0 ownership and asynchronous result bridge for the paired CPU1 firmware.
################################################################################

TDVP_CPU1_VISION_SITE = $(TOPDIR)/board/tdvp/cpu1/vision
TDVP_CPU1_VISION_SITE_METHOD = local
TDVP_CPU1_VISION_LICENSE = GPL-2.0, MIT
TDVP_CPU1_VISION_MODULE_SUBDIRS = linux
TDVP_CPU1_VISION_INSTALL_STAGING = YES

define TDVP_CPU1_VISION_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/tdvp_vision_abi.h $(STAGING_DIR)/usr/include/tdvp/tdvp_vision_abi.h
	$(INSTALL) -D -m 0644 $(@D)/tdvp_ai_abi.h $(STAGING_DIR)/usr/include/tdvp/tdvp_ai_abi.h
endef

define TDVP_CPU1_VISION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/linux/70-tdvp-cpu1-vision.rules $(TARGET_DIR)/usr/lib/udev/rules.d/70-tdvp-cpu1-vision.rules
	$(INSTALL) -D -m 0644 $(@D)/linux/tdvp-cpu1-vision.conf $(TARGET_DIR)/usr/lib/modules-load.d/tdvp-cpu1-vision.conf
endef

$(eval $(kernel-module))
$(eval $(generic-package))

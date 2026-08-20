################################################################################
#
# TDVP K230 KPU acceptance
#
################################################################################

TDVP_KPU_ACCEPTANCE_SITE = $(TOPDIR)/package/tdvp-kpu-acceptance/src
TDVP_KPU_ACCEPTANCE_SITE_METHOD = local
TDVP_KPU_ACCEPTANCE_LICENSE = MIT
TDVP_KPU_ACCEPTANCE_LICENSE_FILES = LICENSE
TDVP_KPU_ACCEPTANCE_DEPENDENCIES = ai2d_kpu

define TDVP_KPU_ACCEPTANCE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-kpu-smoke \
		$(TARGET_DIR)/usr/local/bin/tdvp-kpu-smoke
	$(INSTALL) -D -m 0644 $(@D)/tdvp-kpu-acceptance.service \
		$(TARGET_DIR)/usr/lib/systemd/system/tdvp-kpu-acceptance.service
	@test -x $(TARGET_DIR)/root/app/ai2d_kpu/ai2d_kpu.elf
	@test -f $(TARGET_DIR)/root/app/ai2d_kpu/test.kmodel
	@test -f $(TARGET_DIR)/root/app/ai2d_kpu/ai2d_input.bin
	@test -f $(TARGET_DIR)/root/app/ai2d_kpu/input.bin
	@test -f $(TARGET_DIR)/root/app/ai2d_kpu/result.bin
endef

define TDVP_KPU_ACCEPTANCE_ENABLE_SERVICE
	$(INSTALL) -d $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/tdvp-kpu-acceptance.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/tdvp-kpu-acceptance.service
endef

TDVP_KPU_ACCEPTANCE_POST_INSTALL_TARGET_HOOKS += TDVP_KPU_ACCEPTANCE_ENABLE_SERVICE

$(eval $(generic-package))

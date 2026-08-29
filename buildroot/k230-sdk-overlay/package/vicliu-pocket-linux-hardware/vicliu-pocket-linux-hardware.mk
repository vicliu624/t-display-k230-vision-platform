################################################################################
#
# Vicliu Pocket Linux K230 hardware services
#
################################################################################

VICLIU_POCKET_LINUX_HARDWARE_SITE = $(TOPDIR)/package/vicliu-pocket-linux-hardware/src
VICLIU_POCKET_LINUX_HARDWARE_SITE_METHOD = local
VICLIU_POCKET_LINUX_HARDWARE_LICENSE = MIT
VICLIU_POCKET_LINUX_HARDWARE_LICENSE_FILES = LICENSE

define VICLIU_POCKET_LINUX_HARDWARE_ENABLE_SERVICE
	$(INSTALL) -d \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants \
		$(TARGET_DIR)/etc/systemd/system/sysinit.target.wants \
		$(TARGET_DIR)/etc/systemd/system/timers.target.wants
	ln -sf ../../../../usr/lib/systemd/system/vicliu-pocket-linux-hardware.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/vicliu-pocket-linux-hardware.service
	ln -sf ../../../../usr/lib/systemd/system/tdvp-rootfs-expand.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/tdvp-rootfs-expand.service
	ln -sf ../../../../usr/lib/systemd/system/tdvp-rtc-load.service \
		$(TARGET_DIR)/etc/systemd/system/sysinit.target.wants/tdvp-rtc-load.service
	ln -sf ../../../../usr/lib/systemd/system/tdvp-rtc-restore.service \
		$(TARGET_DIR)/etc/systemd/system/sysinit.target.wants/tdvp-rtc-restore.service
	ln -sf ../../../../usr/lib/systemd/system/tdvp-rtc-writeback.timer \
		$(TARGET_DIR)/etc/systemd/system/timers.target.wants/tdvp-rtc-writeback.timer
endef

VICLIU_POCKET_LINUX_HARDWARE_POST_INSTALL_TARGET_HOOKS += VICLIU_POCKET_LINUX_HARDWARE_ENABLE_SERVICE

$(eval $(cmake-package))

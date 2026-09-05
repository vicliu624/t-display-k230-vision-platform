################################################################################
#
# VG Lite
#
################################################################################

VG_LITE_DIR_NAME := vg_lite
VG_LITE_APP_NAME := vg_lite
VG_LITE_SITE = $(realpath $(TOPDIR))"/package/vg_lite"
VG_LITE_SITE_METHOD = local
VG_LITE_DEPENDENCIES += libdrm
# The vendor library owns a process-global /dev/vg_lite file.  It must never
# leak through Labwc's exec-based desktop autostart into ordinary clients.
VG_LITE_PATCH = $(VG_LITE_SITE)/0001-tdvp-vglite-close-on-exec.patch

# Buildroot implements SITE_METHOD=local through its override-source rsync
# route.  That route intentionally bypasses generic-package's normal PATCH
# phase, so apply the locked patch immediately after the source copy instead.
# Keeping this as a hook makes vg_lite-dirclean followed by vg_lite rebuild the
# exact audited source tree, rather than silently compiling the vendor file.
define VG_LITE_APPLY_TDVP_CLOSE_ON_EXEC_PATCH
	$(APPLY_PATCHES) $(@D) $(VG_LITE_SITE) $(notdir $(VG_LITE_PATCH))
endef
VG_LITE_POST_RSYNC_HOOKS += VG_LITE_APPLY_TDVP_CLOSE_ON_EXEC_PATCH

DRM_CFLAGS = $(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libdrm -I$(STAGING_DIR)/usr/include
DRM_LDFLAGS = -L$(STAGING_DIR)/usr/lib -ldrm

# The vendor Makefile puts -mcpu=c908v before CFLAGS. Pass the selected target
# flags last so the library can also run on scalar CPU0 in the AMP image.
VG_LITE_CFLAGS += $(TARGET_CFLAGS)

ifeq ($(BR2_RISCV_32), y)
VG_LITE_CFLAGS += -march=rv32gcv_xtheadc
DRM_CFLAGS += -march=rv32gcv_xtheadc
endif

ifeq ($(BR2_PACKAGE_VG_LITE_DEMOS),y)
define VG_LITE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" CFLAGS="$(VG_LITE_CFLAGS)" -C $(@D)/VGLite
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" DRM_CFLAGS="$(DRM_CFLAGS)" DRM_LDFLAGS="$(DRM_LDFLAGS)" SDK_DIR=$(@D) -C $(@D)/test
endef

define VG_LITE_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" -C $(@D)/VGLite install
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" SDK_DIR=$(@D) -C $(@D)/test install
endef

VG_LITE_POST_INSTALL_TARGET_HOOKS += VG_LITE_BUILD_DEB

else
define VG_LITE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" CFLAGS="$(VG_LITE_CFLAGS)" -C $(@D)/VGLite
endef

define VG_LITE_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" -C $(@D)/VGLite install
endef
endif

define VG_LITE_BUILD_DEB
	$(call COPYFILE ,$(TARGET_DIR)/usr/bin/vglite_drm,$(@D)/deb/usr/bin/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/bin/vglite_cube,$(@D)/deb/usr/bin/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/bin/tiger,$(@D)/deb/usr/bin/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/bin/linearGrad,$(@D)/deb/usr/bin/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/bin/imgIndex,$(@D)/deb/usr/bin/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/lib/libvg_lite.so,$(@D)/deb/usr/lib/riscv64-linux-gnu/)
	$(call COPYFILE ,$(TARGET_DIR)/usr/lib/libvg_lite_util.so,$(@D)/deb/usr/lib/riscv64-linux-gnu/)
	dpkg -b  $(@D)/deb  $(BINARIES_DIR)/deb/$(call LOWERCASE, k230-$(PKG)).deb
endef

$(eval $(generic-package))

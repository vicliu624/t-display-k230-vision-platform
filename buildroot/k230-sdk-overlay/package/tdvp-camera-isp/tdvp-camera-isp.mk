################################################################################
# TDVP GC2093: one owner of VVCAM modules and the sensor ABI plugin.
################################################################################

TDVP_CAMERA_ISP_SITE = $(TOPDIR)/package/tdvp-camera-isp/src
TDVP_CAMERA_ISP_SITE_METHOD = local
TDVP_CAMERA_ISP_LICENSE = GPL-2.0, MIT, BSD-2-Clause
TDVP_CAMERA_ISP_DEPENDENCIES = tdvp-camera-isp-runtime
TDVP_CAMERA_ISP_MODULE_SUBDIRS = vvcam
TDVP_CAMERA_ISP_MODULE_MAKE_OPTS = BR2_PACKAGE_VVCAM_DEF_SENSOR=gc2093

# Buildroot skips patching for local/override sources. Recreate only our private
# vendor copy on every rsync, including rebuilds: upstream rsync uses -u and can
# otherwise retain newer, already-patched files. Never patch TOPDIR/package/vvcam.
define TDVP_CAMERA_ISP_PREPARE_VENDOR
	test "$(BR2_PACKAGE_VVCAM)" != y
	cd $(@D)/patches && sha256sum -c SHA256SUMS
	mkdir -p $(@D)/vvcam
	rsync -a --delete $(TOPDIR)/package/vvcam/ $(@D)/vvcam/
	set -e; for p in $(@D)/patches/*.patch; do \
		patch --batch --fuzz=0 -d $(@D)/vvcam -p1 < $$p; \
	done
endef
TDVP_CAMERA_ISP_POST_RSYNC_HOOKS += TDVP_CAMERA_ISP_PREPARE_VENDOR

define TDVP_CAMERA_ISP_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=gnu11 -fPIC -shared \
		-I$(@D) -I$(@D)/vvcam/include $(@D)/tdvp-vvcam-legacy-adapter.c \
		$(@D)/tdvp-gc2093-i2c.c $(@D)/vvcam/src/gc2093.c \
		$(@D)/vvcam/src/version.c $(TARGET_LDFLAGS) \
		-Wl,-soname,libvvcam.so -o $(@D)/libvvcam.so
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=c11 -I$(@D) \
		$(@D)/tdvp-gc2093-chip-id.c $(@D)/tdvp-gc2093-i2c.c \
		$(TARGET_LDFLAGS) -o $(@D)/tdvp-gc2093-chip-id
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=c11 \
		$(@D)/tdvp-camera-capture-check.c $(TARGET_LDFLAGS) \
		-o $(@D)/tdvp-camera-capture-check
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=c11 \
		$(@D)/tdvp-camera-device.c $(TARGET_LDFLAGS) -o $(@D)/tdvp-camera-device
endef

define TDVP_CAMERA_ISP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libvvcam.so $(TARGET_DIR)/usr/lib/libvvcam.so
	set -e; for helper in tdvp-gc2093-chip-id tdvp-camera-capture-check tdvp-camera-device tdvp-camera-modules; do \
		$(INSTALL) -D -m 0755 $(@D)/$$helper $(TARGET_DIR)/usr/libexec/$$helper; \
	done
	$(INSTALL) -D -m 0644 $(@D)/70-tdvp-camera.rules $(TARGET_DIR)/usr/lib/udev/rules.d/70-tdvp-camera.rules
endef

define TDVP_CAMERA_ISP_INSTALL_INIT_SYSTEMD
	$(INSTALL) -D -m 0644 $(@D)/tdvp-camera-modules.service $(TARGET_DIR)/usr/lib/systemd/system/tdvp-camera-modules.service
	$(INSTALL) -D -m 0644 $(@D)/tdvp-camera-isp.service $(TARGET_DIR)/usr/lib/systemd/system/tdvp-camera-isp.service
	mkdir -p $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/tdvp-camera-isp.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/tdvp-camera-isp.service
endef

$(eval $(kernel-module))
$(eval $(generic-package))

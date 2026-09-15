################################################################################
# Pinned upstream scalar ISP. Never use the SDK's RVV executable on CPU0.
################################################################################

TDVP_CAMERA_ISP_RUNTIME_VERSION = 155359af908a5fb38e870e81ee82c91bceb30800
TDVP_CAMERA_ISP_RUNTIME_SITE = https://raw.githubusercontent.com/kendryte/k230_linux_sdk/$(TDVP_CAMERA_ISP_RUNTIME_VERSION)/buildroot-overlay/package/vvcam
TDVP_CAMERA_ISP_RUNTIME_SOURCE = isp_media_server
TDVP_CAMERA_ISP_RUNTIME_DEPENDENCIES = mxml
# Upstream supplies this executable without source or a separate license file.
TDVP_CAMERA_ISP_RUNTIME_LICENSE = unknown
TDVP_CAMERA_ISP_RUNTIME_REDISTRIBUTE = NO

define TDVP_CAMERA_ISP_RUNTIME_EXTRACT_CMDS
	$(INSTALL) -D -m 0755 $(TDVP_CAMERA_ISP_RUNTIME_DL_DIR)/$(TDVP_CAMERA_ISP_RUNTIME_SOURCE) $(@D)/isp_media_server
endef

define TDVP_CAMERA_ISP_RUNTIME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/isp_media_server $(TARGET_DIR)/usr/bin/isp_media_server
endef

$(eval $(generic-package))

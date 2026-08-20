################################################################################
#
# tdvp-display-smoke
#
################################################################################

# The package is copied into the staged SDK Buildroot tree by `make sync`.
# Do not rely on the external Buildroot path used by the minimal-system tree.
TDVP_DISPLAY_SMOKE_SITE = $(TOPDIR)/package/tdvp-display-smoke/src
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
	$(INSTALL) -D -m 0755 $(TDVP_DISPLAY_SMOKE_PKGDIR)/src/tdvp-kms-acceptance \
		$(TARGET_DIR)/usr/libexec/tdvp/tdvp-kms-acceptance
	$(INSTALL) -D -m 0644 $(TDVP_DISPLAY_SMOKE_PKGDIR)/src/tdvp-kms-acceptance.service \
		$(TARGET_DIR)/usr/lib/systemd/system/tdvp-kms-acceptance.service
endef

$(eval $(generic-package))

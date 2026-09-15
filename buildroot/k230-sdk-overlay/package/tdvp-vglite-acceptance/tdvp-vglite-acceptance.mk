################################################################################
#
# tdvp-vglite-acceptance
#
################################################################################

TDVP_VGLITE_ACCEPTANCE_SITE = $(TOPDIR)/package/tdvp-vglite-acceptance/src
TDVP_VGLITE_ACCEPTANCE_SITE_METHOD = local
TDVP_VGLITE_ACCEPTANCE_DEPENDENCIES = libdrm vg_lite

define TDVP_VGLITE_ACCEPTANCE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Werror -O2 \
		-I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/libdrm \
		$(@D)/tdvp-vglite-probe.c \
		-o $(@D)/tdvp-vglite-probe \
		$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lvg_lite -ldrm
endef

define TDVP_VGLITE_ACCEPTANCE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-vglite-probe \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-probe
	$(INSTALL) -D -m 0755 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/tdvp-vglite-client-churn \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-client-churn
	$(INSTALL) -D -m 0755 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/tdvp-vglite-watchdog-observer \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-watchdog-observer
	$(INSTALL) -D -m 0755 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/tdvp-vglite-session-gate \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-session-gate
	$(INSTALL) -D -m 0755 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/tdvp-vglite-diagnostics-report \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-diagnostics-report
	$(INSTALL) -D -m 0755 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/tdvp-vglite-inflight-close-gate \
		$(TARGET_DIR)/usr/bin/tdvp-vglite-inflight-close-gate
	$(INSTALL) -D -m 0644 $(TDVP_VGLITE_ACCEPTANCE_PKGDIR)/src/60-tdvp-vg-lite.rules \
		$(TARGET_DIR)/usr/lib/udev/rules.d/60-tdvp-vg-lite.rules
endef

$(eval $(generic-package))

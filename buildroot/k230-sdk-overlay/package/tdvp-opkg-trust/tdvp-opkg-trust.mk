################################################################################
#
# TDVP signed opkg feed trust bootstrap
#
################################################################################

TDVP_OPKG_TRUST_SITE = $(TOPDIR)/package/tdvp-opkg-trust/src
TDVP_OPKG_TRUST_SITE_METHOD = local
TDVP_OPKG_TRUST_LICENSE = MIT
TDVP_OPKG_TRUST_LICENSE_FILES = LICENSE
TDVP_OPKG_TRUST_DEPENDENCIES = gnupg2 opkg

define TDVP_OPKG_TRUST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-opkg-bootstrap \
		$(TARGET_DIR)/usr/local/libexec/tdvp-opkg-bootstrap
	$(INSTALL) -D -m 0755 $(@D)/tdvp-opkg \
		$(TARGET_DIR)/usr/local/sbin/tdvp-opkg
	$(INSTALL) -D -m 0644 $(@D)/tdvp-repo-public.asc \
		$(TARGET_DIR)/usr/share/tdvp/opkg/tdvp-repo-public.asc
endef

$(eval $(generic-package))

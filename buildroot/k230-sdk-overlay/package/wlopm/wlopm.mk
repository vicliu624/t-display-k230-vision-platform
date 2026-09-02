################################################################################
#
# wlopm
#
################################################################################

WLOPM_VERSION = v1.0.0
WLOPM_SITE = https://git.sr.ht/~leon_plickat/wlopm
WLOPM_SITE_METHOD = git
WLOPM_LICENSE = MIT
WLOPM_LICENSE_FILES = LICENSE
WLOPM_DEPENDENCIES = host-wayland wayland

define WLOPM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		SCANNER="$(HOST_DIR)/bin/wayland-scanner"
endef

define WLOPM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/wlopm $(TARGET_DIR)/usr/bin/wlopm
endef

$(eval $(generic-package))

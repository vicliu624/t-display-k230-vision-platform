################################################################################
#
# Vicliu Pocket Linux desktop integration
#
################################################################################

VICLIU_POCKET_LINUX_DESKTOP_SITE = $(TOPDIR)/package/vicliu-pocket-linux-desktop/src
VICLIU_POCKET_LINUX_DESKTOP_SITE_METHOD = local
VICLIU_POCKET_LINUX_DESKTOP_LICENSE = MIT
VICLIU_POCKET_LINUX_DESKTOP_LICENSE_FILES = LICENSE
VICLIU_POCKET_LINUX_DESKTOP_DEPENDENCIES = alsa-utils foot iw mc mpv opkg sudo v4l2grab wayland wofi wvkbd wlr-randr yavta

define VICLIU_POCKET_LINUX_DESKTOP_INSTALL_TARGET_CMDS
	# Browser delivery is deferred to the signed opkg feed.  Remove the old
	# launcher explicitly so an incremental Buildroot target cannot retain a
	# broken Cog/NetSurf entry from a previous product configuration.
	rm -f $(TARGET_DIR)/usr/local/bin/vpl-browser \
		$(TARGET_DIR)/usr/share/applications/vpl-browser.desktop
	$(INSTALL) -D -m 0755 $(@D)/libexec/vpl-desktopctl \
		$(TARGET_DIR)/usr/local/libexec/vpl-desktopctl
	$(INSTALL) -D -m 0440 $(@D)/sudoers/vpl-desktop \
		$(TARGET_DIR)/etc/sudoers.d/vpl-desktop
	$(INSTALL) -D -m 0644 $(@D)/wofi/config \
		$(TARGET_DIR)/etc/xdg/wofi/config
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-camera \
		$(TARGET_DIR)/usr/local/bin/vpl-camera

	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-display-menu \
		$(TARGET_DIR)/usr/local/bin/vpl-display-menu
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-logs \
		$(TARGET_DIR)/usr/local/bin/vpl-logs
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-osk \
		$(TARGET_DIR)/usr/local/bin/vpl-osk
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-package-manager \
		$(TARGET_DIR)/usr/local/bin/vpl-package-manager
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-opkg-console \
		$(TARGET_DIR)/usr/local/bin/vpl-opkg-console
	$(INSTALL) -D -m 0755 $(@D)/bin/vpl-power-menu \
		$(TARGET_DIR)/usr/local/bin/vpl-power-menu
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-camera.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-camera.desktop
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-display.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-display.desktop
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-logs.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-logs.desktop
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-osk.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-osk.desktop
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-package-manager.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-package-manager.desktop
	$(INSTALL) -D -m 0644 $(@D)/applications/vpl-power.desktop \
		$(TARGET_DIR)/usr/share/applications/vpl-power.desktop
	$(INSTALL) -D -m 0644 $(@D)/LICENSE \
		$(TARGET_DIR)/usr/share/doc/vicliu-pocket-linux-desktop/LICENSE
endef

$(eval $(generic-package))

################################################################################
#
# TDVP Labwc desktop session
#
################################################################################

TDVP_LABWC_DESKTOP_SITE = $(TOPDIR)/package/tdvp-labwc-desktop/src
TDVP_LABWC_DESKTOP_SITE_METHOD = local
TDVP_LABWC_DESKTOP_LICENSE = MIT
TDVP_LABWC_DESKTOP_LICENSE_FILES = LICENSE
TDVP_LABWC_DESKTOP_DEPENDENCIES = \
	dbus \
	foot \
	labwc \
	seatd \
	sfwbar \
	swaybg \
	wayland \
	wlr-randr

define TDVP_LABWC_DESKTOP_USERS
	tdvp 1000 tdvp -1 = /home/tdvp /bin/sh seat,input,audio,video,render
endef

define TDVP_LABWC_DESKTOP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-labwc-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-labwc-session
	$(INSTALL) -D -m 0644 $(@D)/environment \
		$(TARGET_DIR)/etc/tdvp/labwc/environment
	$(INSTALL) -D -m 0644 $(@D)/70-tdvp-touch.rules \
		$(TARGET_DIR)/etc/udev/rules.d/70-tdvp-touch.rules
	$(INSTALL) -D -m 0755 $(@D)/autostart \
		$(TARGET_DIR)/etc/xdg/labwc/autostart
	$(INSTALL) -D -m 0755 $(@D)/tdvp-sfwbar-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-sfwbar-session
	$(INSTALL) -D -m 0644 $(@D)/backgrounds/tdvp-pda-paper.svg \
		$(TARGET_DIR)/usr/share/backgrounds/tdvp-pda-paper.svg
	$(INSTALL) -D -m 0644 $(@D)/backgrounds/tdvp-pda-paper.png \
		$(TARGET_DIR)/usr/share/backgrounds/tdvp-pda-paper.png
	$(INSTALL) -D -m 0644 $(@D)/rc.xml \
		$(TARGET_DIR)/etc/xdg/labwc/rc.xml
	$(INSTALL) -D -m 0644 $(@D)/foot.ini \
		$(TARGET_DIR)/etc/xdg/foot/foot.ini
	$(INSTALL) -D -m 0644 $(@D)/foot.desktop \
		$(TARGET_DIR)/usr/share/applications/foot.desktop
	$(INSTALL) -D -m 0644 $(@D)/tdvp-labwc.desktop \
		$(TARGET_DIR)/usr/share/wayland-sessions/tdvp-labwc.desktop
	$(INSTALL) -D -m 0644 $(@D)/sfwbar.config \
		$(TARGET_DIR)/etc/sfwbar/sfwbar.config
	$(INSTALL) -D -m 0644 $(@D)/tdvp-launcher.widget \
		$(TARGET_DIR)/usr/share/sfwbar/tdvp-launcher.widget
	$(INSTALL) -D -m 0644 $(@D)/LICENSE \
		$(TARGET_DIR)/usr/share/doc/tdvp-labwc-desktop/LICENSE
endef

define TDVP_LABWC_DESKTOP_ENABLE_SERVICES
	$(INSTALL) -d $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/seatd.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/seatd.service
endef

TDVP_LABWC_DESKTOP_POST_INSTALL_TARGET_HOOKS += TDVP_LABWC_DESKTOP_ENABLE_SERVICES

$(eval $(generic-package))

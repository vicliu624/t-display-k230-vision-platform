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
	pcmanfm \
	pulseaudio \
	seatd \
	wayland \
	wofi \
	wf-panel-pi \
	wfplug-batt \
	wfplug-clock \
	wfplug-netman \
	wfplug-power \
	wfplug-volumepulse \
	wfplug-menu \
	wlr-randr

define TDVP_LABWC_DESKTOP_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -std=c11 -O2 -Wall -Wextra \
		-o $(@D)/tdvp-key-bridge $(@D)/tdvp-key-bridge.c
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -std=c11 -O2 -Wall -Wextra \
		-fPIC -shared -o $(@D)/tdvp-gdk-committed-compat.so \
		$(@D)/tdvp-gdk-committed-compat.c -ldl -lglib-2.0
endef

define TDVP_LABWC_DESKTOP_USERS
	tdvp 1000 tdvp -1 = /home/tdvp /bin/sh seat,input,audio,video,render
endef

define TDVP_LABWC_DESKTOP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-labwc-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-labwc-session
	$(INSTALL) -D -m 0755 $(@D)/tdvp-terminal \
		$(TARGET_DIR)/usr/local/bin/tdvp-terminal
	$(INSTALL) -D -m 0644 $(@D)/environment \
		$(TARGET_DIR)/etc/tdvp/labwc/environment
	$(INSTALL) -D -m 0644 $(@D)/tdvp-local-admin-path.sh \
		$(TARGET_DIR)/etc/profile.d/tdvp-local-admin-path.sh
	$(INSTALL) -D -m 0644 $(@D)/70-tdvp-touch.rules \
		$(TARGET_DIR)/etc/udev/rules.d/70-tdvp-touch.rules
	$(INSTALL) -D -m 0755 $(@D)/autostart \
		$(TARGET_DIR)/etc/xdg/labwc/autostart
	$(INSTALL) -D -m 0755 $(@D)/tdvp-wf-panel-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-wf-panel-session
	$(INSTALL) -D -m 0755 $(@D)/tdvp-pulseaudio-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-pulseaudio-session
	$(INSTALL) -D -m 0755 $(@D)/tdvp-panel-menu \
		$(TARGET_DIR)/usr/local/bin/tdvp-panel-menu
	$(INSTALL) -D -m 0755 $(@D)/tdvp-key-bridge \
		$(TARGET_DIR)/usr/local/bin/tdvp-key-bridge
	$(INSTALL) -D -m 0755 $(@D)/tdvp-gdk-committed-compat.so \
		$(TARGET_DIR)/usr/lib/tdvp-gdk-committed-compat.so
	$(INSTALL) -D -m 0755 $(@D)/tdvp-pcmanfm-desktop-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-pcmanfm-desktop-session
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
	$(INSTALL) -D -m 0644 $(@D)/tdvp-pcmanfm.desktop \
		$(TARGET_DIR)/usr/share/applications/tdvp-pcmanfm.desktop
	$(INSTALL) -D -m 0644 $(@D)/tdvp-labwc.desktop \
		$(TARGET_DIR)/usr/share/wayland-sessions/tdvp-labwc.desktop
	$(INSTALL) -D -m 0644 $(@D)/wf-panel-pi.ini \
		$(TARGET_DIR)/etc/xdg/wf-panel-pi/wf-panel-pi.ini
	$(INSTALL) -D -m 0644 $(@D)/tdvp-wf-panel.css \
		$(TARGET_DIR)/etc/wf-panel-pi/tdvp.css
	$(INSTALL) -D -m 0644 $(@D)/pcmanfm.conf \
		$(TARGET_DIR)/etc/xdg/pcmanfm/default/pcmanfm.conf
	# wfplug-menu looks up this standard XDG menu name.  Keep the compatibility
	# name expected by the upstream Raspberry Pi module, while its entries are
	# strictly TDVP's own desktop applications.
	$(INSTALL) -D -m 0644 $(@D)/menus/lxde-applications.menu \
		$(TARGET_DIR)/etc/xdg/menus/lxde-applications.menu
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-accessories.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-accessories.directory
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-sound-video.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-sound-video.directory
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-games.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-games.directory
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-internet.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-internet.directory
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-preferences.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-preferences.directory
	$(INSTALL) -D -m 0644 $(@D)/menus/tdvp-system.directory \
		$(TARGET_DIR)/usr/share/desktop-directories/tdvp-system.directory
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

################################################################################
#
# tdvp-keyboard-layout
#
################################################################################

TDVP_KEYBOARD_LAYOUT_SITE = $(TOPDIR)/package/tdvp-keyboard-layout/src
TDVP_KEYBOARD_LAYOUT_SITE_METHOD = local

define TDVP_KEYBOARD_LAYOUT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/t-display-k230.map \
		$(TARGET_DIR)/etc/tdvp/keymaps/t-display-k230.map
	$(INSTALL) -D -m 0644 $(@D)/tdvp-fn-yellow.xkb \
		$(TARGET_DIR)/etc/tdvp/keymaps/tdvp-fn-yellow.xkb
	$(INSTALL) -D -m 0644 $(@D)/tdvp-us-xkb-variant.xkb \
		$(TARGET_DIR)/etc/tdvp/keymaps/tdvp-us-xkb-variant.xkb
	$(INSTALL) -D -m 0755 $(@D)/S25tdvp-keyboard-layout \
		$(TARGET_DIR)/etc/init.d/S25tdvp-keyboard-layout
	$(INSTALL) -D -m 0644 $(@D)/tdvp-keyboard-layout.service \
		$(TARGET_DIR)/usr/lib/systemd/system/tdvp-keyboard-layout.service
	$(INSTALL) -d $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/tdvp-keyboard-layout.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/tdvp-keyboard-layout.service
endef

$(eval $(generic-package))

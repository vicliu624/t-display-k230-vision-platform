################################################################################
#
# TDVP graphical login session
#
################################################################################

TDVP_GREETER_SITE = $(TOPDIR)/package/tdvp-greeter/src
TDVP_GREETER_SITE_METHOD = local
TDVP_GREETER_LICENSE = MIT
TDVP_GREETER_LICENSE_FILES = LICENSE
TDVP_GREETER_DEPENDENCIES = \
	dbus \
	labwc \
	seatd \
	tdvp-greetd \
	tdvp-gtkgreet \
	wlr-randr
# The image starts the tdvp desktop directly.  The greeter stays installed as
# an explicit rollback target selected by tdvp-graphical-login, and shares the
# same board DRM and output-transform contract when it is selected.
define TDVP_GREETER_USERS
	greeter -1 greeter -1 = /var/lib/greetd /bin/sh seat,video,render
endef

define TDVP_GREETER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tdvp-graphical-login \
		$(TARGET_DIR)/usr/local/sbin/tdvp-graphical-login
	$(INSTALL) -D -m 0755 $(@D)/tdvp-greeter-session \
		$(TARGET_DIR)/usr/local/bin/tdvp-greeter-session
	$(INSTALL) -D -m 0755 $(@D)/tdvp-greeter-labwc \
		$(TARGET_DIR)/usr/local/bin/tdvp-greeter-labwc
	$(INSTALL) -D -m 0644 $(@D)/config.toml \
		$(TARGET_DIR)/etc/greetd/config.toml
	$(INSTALL) -D -m 0644 $(@D)/greetd.service \
		$(TARGET_DIR)/usr/lib/systemd/system/greetd.service
	$(INSTALL) -D -m 0644 $(@D)/greetd \
		$(TARGET_DIR)/etc/pam.d/greetd
	$(INSTALL) -D -m 0644 $(@D)/greetd-greeter \
		$(TARGET_DIR)/etc/pam.d/greetd-greeter
	$(INSTALL) -D -m 0644 $(@D)/gtkgreet.css \
		$(TARGET_DIR)/etc/greetd/gtkgreet.css
	$(INSTALL) -D -m 0644 $(@D)/labwc/rc.xml \
		$(TARGET_DIR)/etc/tdvp/greetd/labwc/rc.xml
	$(INSTALL) -D -m 0755 $(@D)/labwc/autostart \
		$(TARGET_DIR)/etc/tdvp/greetd/labwc/autostart
	$(INSTALL) -D -m 0644 $(@D)/LICENSE \
		$(TARGET_DIR)/usr/share/doc/tdvp-greeter/LICENSE
endef

define TDVP_GREETER_ENABLE_SERVICE
	$(INSTALL) -d $(TARGET_DIR)/etc/systemd/system/multi-user.target.wants
	ln -sf ../../../../usr/lib/systemd/system/greetd.service \
		$(TARGET_DIR)/etc/systemd/system/multi-user.target.wants/greetd.service
endef

TDVP_GREETER_POST_INSTALL_TARGET_HOOKS += TDVP_GREETER_ENABLE_SERVICE

$(eval $(generic-package))

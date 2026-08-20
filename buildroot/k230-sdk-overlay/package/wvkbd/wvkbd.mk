################################################################################
#
# wvkbd
#
################################################################################

# Keep this release pin explicit while the keyboard package is validated
# against the K230 external RISC-V toolchain.  The package recipe must use
# TARGET_CONFIGURE_OPTS below; TARGET_MAKE_ENV alone leaves CC as the build
# host compiler and can silently create x86-64 objects.
WVKBD_VERSION = v0.18
WVKBD_SITE = $(call github,jjsullivan5196,wvkbd,$(WVKBD_VERSION))
WVKBD_LICENSE = GPL-3.0+
WVKBD_LICENSE_FILES = COPYING
WVKBD_DEPENDENCIES = host-pkgconf fontconfig libxkbcommon pango pixman wayland

define WVKBD_BUILD_CMDS
	# The target image installs only the keyboard binary.  Upstream's `all`
	# target also builds a man page with the host-only scdoc utility, which is
	# neither shipped nor needed by the embedded image.
	$(TARGET_CONFIGURE_OPTS) $(MAKE) -C $(@D) LAYOUT=mobintl DOCS=
endef

define WVKBD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/wvkbd-mobintl \
		$(TARGET_DIR)/usr/bin/wvkbd-mobintl
endef

$(eval $(generic-package))

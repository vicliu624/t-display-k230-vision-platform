################################################################################
#
# greetd
#
################################################################################

TDVP_GREETD_VERSION = 0.10.3
TDVP_GREETD_SITE = https://git.sr.ht/~kennylevinsen/greetd/archive
TDVP_GREETD_SOURCE = $(TDVP_GREETD_VERSION).tar.gz
TDVP_GREETD_SUBDIR = greetd
TDVP_GREETD_LICENSE = GPL-3.0-only
TDVP_GREETD_LICENSE_FILES = LICENSE
TDVP_GREETD_DEPENDENCIES = host-pkgconf linux-pam

define TDVP_GREETD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/target/$(RUSTC_TARGET_NAME)/release/greetd \
		$(TARGET_DIR)/usr/bin/greetd
endef

$(eval $(cargo-package))

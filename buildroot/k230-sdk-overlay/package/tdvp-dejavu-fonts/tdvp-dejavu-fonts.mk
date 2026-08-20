################################################################################
#
# tdvp-dejavu-fonts
#
################################################################################

TDVP_DEJAVU_FONTS_VERSION = 2.37
TDVP_DEJAVU_FONTS_SITE = https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37
TDVP_DEJAVU_FONTS_SOURCE = dejavu-fonts-ttf-$(TDVP_DEJAVU_FONTS_VERSION).tar.bz2
TDVP_DEJAVU_FONTS_SITE_METHOD = wget
TDVP_DEJAVU_FONTS_LICENSE = Bitstream Vera, public domain, MIT
TDVP_DEJAVU_FONTS_LICENSE_FILES = LICENSE

define TDVP_DEJAVU_FONTS_INSTALL_TARGET_CMDS
	$(INSTALL) -d $(TARGET_DIR)/usr/share/fonts/truetype/dejavu
	$(INSTALL) -m 0644 $(@D)/ttf/*.ttf \
		$(TARGET_DIR)/usr/share/fonts/truetype/dejavu/
endef

$(eval $(generic-package))

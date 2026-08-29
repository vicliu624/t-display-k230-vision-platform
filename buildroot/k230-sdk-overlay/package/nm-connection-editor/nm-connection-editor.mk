################################################################################
#
# NetworkManager Connection Editor (without the GNOME panel applet)
#
################################################################################

# 1.36.0 is an annotated tag; pin the resolved source commit, not the tag
# object, so the source used for a release is unambiguous.
NM_CONNECTION_EDITOR_VERSION = 8accd508caa0400304a01da718eeab587ee8fb04
NM_CONNECTION_EDITOR_SITE = https://gitlab.gnome.org/GNOME/network-manager-applet.git
NM_CONNECTION_EDITOR_SITE_METHOD = git
NM_CONNECTION_EDITOR_LICENSE = GPL-2.0+
NM_CONNECTION_EDITOR_LICENSE_FILES = COPYING
NM_CONNECTION_EDITOR_DEPENDENCIES = \
	libgtk3 \
	libnma \
	libsecret \
	network-manager
NM_CONNECTION_EDITOR_CONF_OPTS = \
	-Dappindicator=no \
	-Dwwan=false \
	-Dselinux=false \
	-Dteam=false \
	-Dmore_asserts=no

$(eval $(meson-package))

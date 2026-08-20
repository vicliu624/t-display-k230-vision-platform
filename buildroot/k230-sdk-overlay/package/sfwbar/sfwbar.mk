################################################################################
#
# sfwbar
#
################################################################################

SFWBAR_VERSION = 2afac3a3201b947e76e09ac7822ff2bc020073f3
SFWBAR_SITE = https://github.com/LBCrion/sfwbar.git
SFWBAR_SITE_METHOD = git
SFWBAR_LICENSE = GPL-3.0-only
SFWBAR_LICENSE_FILES = LICENSE
SFWBAR_DEPENDENCIES = \
	dbus \
	gtk-layer-shell \
	host-pkgconf \
	host-wayland \
	json-c \
	libgtk3 \
	wayland \
	wayland-protocols
SFWBAR_CONF_OPTS = \
	-Dalsa=disabled \
	-Dappmenu=enabled \
	-Dbluez=disabled \
	-Ddbus=enabled \
	-Diwd=disabled \
	-Dnm=disabled \
	-Dncenter=disabled \
	-Dbsdctl=disabled \
	-Didle=disabled \
	-Didleinhibit=disabled \
	-Dnetwork=disabled \
	-Dpulse=disabled \
	-Dpipewire=disabled \
	-Dmpd=disabled \
	-Dxkb=disabled \
	-Dbuild-docs=disabled

$(eval $(meson-package))

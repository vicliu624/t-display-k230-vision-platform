################################################################################
#
# gtk-layer-shell
#
################################################################################

GTK_LAYER_SHELL_VERSION = 0.8.0
GTK_LAYER_SHELL_SOURCE = v$(GTK_LAYER_SHELL_VERSION).tar.gz
GTK_LAYER_SHELL_SITE = https://github.com/wmww/gtk-layer-shell/archive/refs/tags
GTK_LAYER_SHELL_LICENSE = LGPL-3.0-only
GTK_LAYER_SHELL_LICENSE_FILES = LICENSE_LGPL.txt
GTK_LAYER_SHELL_INSTALL_STAGING = YES
GTK_LAYER_SHELL_DEPENDENCIES = \
	host-pkgconf \
	host-wayland \
	libgtk3 \
	wayland \
	wayland-protocols
GTK_LAYER_SHELL_CONF_OPTS = \
	-Ddocs=false \
	-Dexamples=false \
	-Dintrospection=false \
	-Dtests=false \
	-Dvapi=false

$(eval $(meson-package))

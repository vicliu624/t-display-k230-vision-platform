################################################################################
#
# wlroots
#
################################################################################

# Pin the K230 renderer implementation by immutable commit.  The package still
# provides the upstream wlroots 0.18 ABI (libwlroots-0.18.so).
WLROOTS_VERSION = 94bca3e871ec4cce73afbef7bad4d962331ab9bb
WLROOTS_SITE = https://github.com/vicliu624/wlroots-vglite.git
WLROOTS_SITE_METHOD = git
WLROOTS_LICENSE = MIT
WLROOTS_LICENSE_FILES = LICENSE
WLROOTS_INSTALL_STAGING = YES

WLROOTS_DEPENDENCIES = \
	host-pkgconf \
	host-wayland \
	hwdata \
	libdisplay-info \
	libinput \
	libxkbcommon \
	libegl \
	libgles \
	pixman \
	seatd \
	udev \
	vg_lite \
	wayland \
	wayland-protocols

WLROOTS_CONF_OPTS = -Dexamples=false -Dxcb-errors=disabled

# wlroots 0.18.2 with the pinned TDVP fork exposes the K230 VGLite renderer.
# The renderer implementation uses DRM dumb buffers exported through PRIME
# DMA-BUF and does not use EGL. The runtime renderer is selected by the
# session environment; the default target profile remains Pixman during VGLite
# staging.
WLROOTS_RENDERERS = vglite
WLROOTS_BACKENDS = libinput drm

ifeq ($(BR2_PACKAGE_WLROOTS_X11),y)
WLROOTS_BACKENDS += x11
WLROOTS_DEPENDENCIES += libxcb xcb-util-wm xcb-util-renderutil xlib_libX11
endif

ifeq ($(BR2_PACKAGE_WLROOTS_XWAYLAND),y)
WLROOTS_CONF_OPTS += -Dxwayland=enabled
WLROOTS_DEPENDENCIES += libxcb xcb-util-wm xwayland
else
WLROOTS_CONF_OPTS += -Dxwayland=disabled
endif

ifeq ($(BR2_PACKAGE_MESA3D_VULKAN_DRIVER)$(BR2_PACKAGE_VULKAN_LOADER),yy)
WLROOTS_RENDERERS += vulkan
WLROOTS_DEPENDENCIES += mesa3d vulkan-loader
endif

WLROOTS_CONF_OPTS += \
	-Dbackends=$(subst $(space),$(comma),$(strip $(WLROOTS_BACKENDS))) \
	-Drenderers=$(subst $(space),$(comma),$(strip $(WLROOTS_RENDERERS)))

$(eval $(meson-package))

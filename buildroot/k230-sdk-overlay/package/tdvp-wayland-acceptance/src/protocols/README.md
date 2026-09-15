# Protocol source

`wlr-layer-shell-unstable-v1.xml` is an unmodified copy from gtk-layer-shell
v0.8.0, the same release used by the desktop image:

https://github.com/wmww/gtk-layer-shell/blob/v0.8.0/protocol/wlr-layer-shell-unstable-v1.xml

Its permissive copyright/license notice is retained inside the XML. Vendoring
the protocol keeps the low-level SHM acceptance client independent of GTK and
avoids an unpinned protocol download during image builds. Generate the client
header and private code using the build host's wayland-scanner, never a target
RISC-V executable.

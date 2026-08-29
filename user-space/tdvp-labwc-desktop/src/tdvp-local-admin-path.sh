#!/bin/sh
# Buildroot's default profile omits /usr/local/sbin for non-root users. TDVP
# installs its deliberately restricted administration wrappers there, so make
# them discoverable in SSH and local interactive shells as well as in Labwc.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

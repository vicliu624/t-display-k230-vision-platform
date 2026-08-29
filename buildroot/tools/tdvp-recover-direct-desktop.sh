#!/bin/sh
# Restore the supported graphical login after an interrupted session test.
# A direct tdvp-labwc-desktop system service is retired: graphical applications
# must run in the account that greetd actually authenticates.
set -eu

systemctl reset-failed greetd.service
systemctl enable --now greetd.service
systemctl --no-pager --full status greetd.service

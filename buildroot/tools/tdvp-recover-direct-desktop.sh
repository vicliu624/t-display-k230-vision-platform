#!/bin/sh
# Restore the known-good direct Labwc session after an interrupted greeter test.
set -eu

systemctl disable --now greetd.service 2>/dev/null || true
systemctl enable --now tdvp-labwc-desktop.service
systemctl --no-pager --full status tdvp-labwc-desktop.service

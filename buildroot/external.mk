# T-Display K230 Vision Platform br2-external make integration.
#
# Platform packages live under buildroot/package/<name>/<name>.mk.
# Keep official Buildroot sources in buildroot/buildroot untouched.

include $(sort $(wildcard $(BR2_EXTERNAL_TDVP_PATH)/package/*/*.mk))

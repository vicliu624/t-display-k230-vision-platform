#pragma once

#include "contract.hpp"

namespace vpl::hardware {

// The platform may have no Bluetooth transport at all.  These helpers only
// report an adapter after the kernel has registered hci* and BlueZ has exposed
// a corresponding Adapter1 object on the system D-Bus.
[[nodiscard]] bool bluetooth_hci_present();
void append_bluetooth_state(State *state);

// BlueZ owns Adapter1.Powered.  The root-owned Quick Settings provider uses
// these helpers so the unprivileged Wayland UI never receives raw radio or
// D-Bus authority.
[[nodiscard]] bool ensure_bluetooth_service();
[[nodiscard]] bool set_bluetooth_power(bool enabled);

}  // namespace vpl::hardware

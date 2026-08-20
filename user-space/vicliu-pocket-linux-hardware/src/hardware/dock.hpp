#pragma once

#include "contract.hpp"

namespace vpl::hardware {

struct DockState {
    bool keyboard_input = false;
    bool keyboard_i2c = false;
    bool keyboard_backlight = false;
    bool attached = false;
};

DockState read_dock_state();
void append_dock_state(State *state, const DockState &dock);

}  // namespace vpl::hardware

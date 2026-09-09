#pragma once
#include "contract.hpp"

namespace vpl::hardware {
// Reads the camera ownership and AI job bridges' telemetry only; never opens
// either job device, starts a stream, writes a mailbox, or trusts old markers.
void append_cpu1_vision_state(State *state);
}

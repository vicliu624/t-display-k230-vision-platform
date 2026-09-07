#pragma once
#include "contract.hpp"

namespace vpl::hardware {
// Reads the kernel bridge's telemetry only; never opens /dev/tdvp-vision,
// starts a stream, writes a mailbox, or trusts old Linux acceptance markers.
void append_cpu1_vision_state(State *state);
}

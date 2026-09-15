#pragma once

#include "contract.hpp"

namespace vpl::hardware {
// Read-only inventory. Never switches the shared radio profile or powers it on.
void append_lora_state(State *state);
}

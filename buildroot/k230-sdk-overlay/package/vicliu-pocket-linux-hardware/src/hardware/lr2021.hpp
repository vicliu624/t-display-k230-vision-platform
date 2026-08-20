#pragma once

#include <cstdint>
#include <string>

namespace vpl::hardware {

struct Lr2021ProbeResult {
    bool profile_selected = false;
    bool power_enabled = false;
    bool power_disabled = false;
    bool command_sent = false;
    bool response_valid = false;
    uint8_t firmware_major = 0;
    uint8_t firmware_minor = 0;
    std::string error;
};

Lr2021ProbeResult probe_lr2021();

}  // namespace vpl::hardware

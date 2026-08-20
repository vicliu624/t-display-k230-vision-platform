#pragma once

#include <string>

namespace vpl::hardware {

struct Nrf9151ProbeResult {
    bool profile_available = false;
    bool profile_selected = false;
    bool uart_opened = false;
    bool uart_configured = false;
    bool restored_profile = false;
    std::string original_profile;
    std::string response;
    std::string error;
};

Nrf9151ProbeResult probe_nrf9151();

}  // namespace vpl::hardware

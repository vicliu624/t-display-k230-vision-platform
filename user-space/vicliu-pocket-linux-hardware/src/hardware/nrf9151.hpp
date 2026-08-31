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

struct Nrf9151GnssResult {
    bool uart_opened = false;
    bool uart_configured = false;
    bool mode_configured = false;
    bool gnss_active = false;
    std::string response;
    std::string error;
};

// The caller owns the board radio-mux profile and must have selected
// "nrf9151" before calling this modem-only operation.  It uses Nordic's
// documented %XSYSTEMMODE / +CFUN command sequence and does not write the
// system's own radio-mux GPIOs.
Nrf9151GnssResult set_nrf9151_gnss(bool enabled);

}  // namespace vpl::hardware

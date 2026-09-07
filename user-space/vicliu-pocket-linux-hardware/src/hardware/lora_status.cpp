#include "lora_status.hpp"
#include "paths.hpp"

namespace vpl::hardware {

void append_lora_state(State *state)
{
    if (state == nullptr)
        return;
    const auto dock = state->find("dock_profile");
    const bool attached = dock != state->end() && dock->second == "attached";
    const bool transport = attached && paths::exists("/dev/spidev0.0");
    const bool driver = transport && paths::exists("/sys/bus/spi/devices/spi0.0/driver") &&
        paths::exists("/sys/devices/platform/radio-mux/profile") &&
        paths::exists("/sys/devices/platform/radio-mux/lora_state");
    const bool runtime = paths::executable("/usr/local/bin/vpl-lora-probe");
    const bool available = transport && driver && runtime;
    const std::string profile = paths::read("/sys/devices/platform/radio-mux/profile");
    const std::string power = paths::read("/sys/devices/platform/radio-mux/lora_state");
    const bool enabled = available && profile == "lora" && power == "on";
    // A registered SPI device or installed probe is not RF acceptance.
    // Preserve the explicit session-scoped acceptance contract.
    const bool functional = available && paths::exists(
        "/run/vicliu-pocket-linux-hardware/acceptance/lora.pass");
    (*state)["lora_transport"] = transport ? "1" : "0";
    (*state)["lora_driver"] = driver ? "1" : "0";
    (*state)["lora_runtime"] = runtime ? "1" : "0";
    (*state)["lora_available"] = available ? "1" : "0";
    (*state)["lora_control_available"] = available ? "1" : "0";
    (*state)["lora_enabled"] = enabled ? "1" : "0";
    (*state)["lora_requested"] = enabled ? "1" : "0";
    (*state)["lora_functional"] = functional ? "1" : "0";
    (*state)["lora_active"] = functional && enabled ? "1" : "0";
    (*state)["lora_acceptance"] = functional ? "passed" :
        (available ? "unverified" : "unavailable");
    (*state)["lora_state"] = !available ? "unavailable" :
        (profile != "lora" ? "resource-busy" :
         (power == "on" ? "on" : (power == "off" ? "off" : "unknown")));
    (*state)["radio_profile"] = profile;
}

}  // namespace vpl::hardware

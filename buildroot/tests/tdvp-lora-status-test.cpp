#include "lora_status.hpp"
#include "paths.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

static std::set<std::string> files;
static std::map<std::string, std::string> contents;
namespace vpl::hardware::paths {
bool exists(const std::string &path) { return files.count(path) != 0; }
bool executable(const std::string &path) { return files.count(path) != 0; }
std::string read(const std::string &path) { return contents[path]; }
// Deliberately no write()/run() stubs: state collection must remain read-only.
}

int main()
{
    using namespace vpl::hardware;
    State state{{"dock_profile", "attached"}};
    append_lora_state(nullptr);
    append_lora_state(&state);
    assert(state["lora_available"] == "0");
    const std::string profile = "/sys/devices/platform/radio-mux/profile";
    const std::string power = "/sys/devices/platform/radio-mux/lora_state";
    const std::string spi_driver = "/sys/bus/spi/devices/spi0.0/driver";
    const std::string runtime = "/usr/local/bin/vpl-lora-probe";
    files = {"/dev/spidev0.0", spi_driver, profile, power, runtime};
    contents[profile] = "lora";
    contents[power] = "off";
    append_lora_state(&state);
    assert(state["lora_transport"] == "1" && state["lora_driver"] == "1");
    assert(state["lora_runtime"] == "1" && state["lora_available"] == "1");
    assert(state["lora_enabled"] == "0" && state["lora_state"] == "off");
    assert(state["lora_acceptance"] == "unverified" && state["lora_functional"] == "0");
    contents[power] = "on";
    append_lora_state(&state);
    assert(state["lora_enabled"] == "1" && state["lora_functional"] == "0");
    files.insert("/run/vicliu-pocket-linux-hardware/acceptance/lora.pass");
    append_lora_state(&state);
    assert(state["lora_acceptance"] == "passed" && state["lora_active"] == "1");
    contents[profile] = "nrf9151";
    append_lora_state(&state);
    assert(state["lora_state"] == "resource-busy" && state["lora_enabled"] == "0");
    assert(state["lora_active"] == "0");
    files.erase(spi_driver);
    append_lora_state(&state);
    assert(state["lora_transport"] == "1" && state["lora_driver"] == "0");
    assert(state["lora_available"] == "0" && state["lora_functional"] == "0");
    files.insert(spi_driver);
    files.erase(runtime);
    append_lora_state(&state);
    assert(state["lora_driver"] == "1" && state["lora_runtime"] == "0");
    assert(state["lora_available"] == "0");
    files.insert(runtime);
    state["dock_profile"] = "detached";
    append_lora_state(&state);
    assert(state["lora_available"] == "0" && state["lora_transport"] == "0");
    assert(state["lora_control_available"] == "0" && state["lora_functional"] == "0");
    std::cout << "tdvp-lora-status-test: PASS inventory, power, mux, absence, acceptance\n";
}

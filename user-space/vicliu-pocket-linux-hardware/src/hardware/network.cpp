#include "network.hpp"

#include "paths.hpp"

#include <string>

namespace vpl::hardware {
namespace {

void put(State *state, const std::string &key, bool value)
{
    (*state)[key] = value ? "1" : "0";
}

bool carrier(const std::string &interface_name)
{
    return paths::read("/sys/class/net/" + interface_name + "/carrier") == "1";
}

bool has_ipv4_default_route()
{
    const std::string routes = paths::read("/proc/net/route");
    return routes.find("\t00000000\t") != std::string::npos;
}

bool network_manager_service()
{
    return paths::service_active("NetworkManager.service");
}

bool wifi_radio_enabled()
{
    bool wlan_rfkill_found = false;
    for (const std::string &entry : paths::children("/sys/class/rfkill")) {
        const std::string base = "/sys/class/rfkill/" + entry;
        if (paths::read(base + "/type") != "wlan")
            continue;
        wlan_rfkill_found = true;
        if (paths::read(base + "/state") == "1")
            return true;
    }
    // Some USB Wi-Fi drivers do not expose a dedicated rfkill node.  In that
    // case availability is still the only kernel-level signal; NetworkManager
    // owns the actual radio policy and the UI treats the state as enabled.
    return !wlan_rfkill_found;
}

}  // namespace

void append_network_state(State *state)
{
    const bool wifi_device = paths::exists("/sys/class/net/wlan0");
    const bool network_manager = network_manager_service();
    const bool wifi_carrier = wifi_device && carrier("wlan0");
    put(state, "wifi_transport", wifi_device);
    put(state, "wifi_driver", wifi_device);
    put(state, "wifi_runtime", network_manager);
    put(state, "wifi_link", wifi_carrier);
    put(state, "wifi_available", wifi_device && network_manager);
    // Radio state comes from the kernel rfkill class when the driver exposes
    // it.  Link remains a separate signal and must never be inferred from
    // radio availability.
    put(state, "wifi_enabled", wifi_device && network_manager && wifi_radio_enabled());
    state->emplace("wifi_operstate", paths::read("/sys/class/net/wlan0/operstate"));

    bool ethernet_device = false;
    bool ethernet_carrier = false;
    for (const std::string &entry : paths::children("/sys/class/net")) {
        if (entry.rfind("en", 0) != 0 && entry.rfind("eth", 0) != 0)
            continue;
        ethernet_device = true;
        ethernet_carrier = ethernet_carrier || carrier(entry);
    }
    put(state, "ethernet_transport", ethernet_device);
    put(state, "ethernet_driver", ethernet_device);
    put(state, "ethernet_link", ethernet_carrier);
    put(state, "ethernet_available", ethernet_device);
    put(state, "ipv4_default_route_present", has_ipv4_default_route());
}

}  // namespace vpl::hardware

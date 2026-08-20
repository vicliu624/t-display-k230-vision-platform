#include "dock.hpp"

#include "paths.hpp"

#include <string>

namespace vpl::hardware {
namespace {

bool input_named(const std::string &needle)
{
    for (const std::string &entry : paths::children("/sys/class/input")) {
        const std::string name = paths::read("/sys/class/input/" + entry + "/device/name");
        if (name.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool i2c_driver_bound(const std::string &driver)
{
    for (const std::string &entry : paths::children("/sys/bus/i2c/devices")) {
        const std::string modalias = paths::read("/sys/bus/i2c/devices/" + entry + "/modalias");
        if (modalias.find(driver) != std::string::npos)
            return true;
    }
    return false;
}

bool backlight_named(const std::string &needle)
{
    for (const std::string &entry : paths::children("/sys/class/backlight")) {
        if (entry.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

void put(State *state, const std::string &key, bool value)
{
    (*state)[key] = value ? "1" : "0";
}

}  // namespace

DockState read_dock_state()
{
    DockState state;
    state.keyboard_input = input_named("TCA8418");
    state.keyboard_i2c = i2c_driver_bound("tca8418");
    state.keyboard_backlight = backlight_named("keyboard");
    state.attached = state.keyboard_input && state.keyboard_i2c;
    return state;
}

void append_dock_state(State *state, const DockState &dock)
{
    state->emplace("dock_profile", dock.attached ? "attached" : "detached-or-unproven");
    put(state, "dock_keyboard_input", dock.keyboard_input);
    put(state, "dock_keyboard_i2c", dock.keyboard_i2c);
    put(state, "dock_keyboard_backlight", dock.keyboard_backlight);
    state->emplace("dock_nrf9151_sku_state", "unverified");
    state->emplace("dock_power_devices_state", "kernel-driver-unverified");
}

}  // namespace vpl::hardware

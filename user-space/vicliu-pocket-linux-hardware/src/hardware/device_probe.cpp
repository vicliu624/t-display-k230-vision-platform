#include "device_probe.hpp"

#include "paths.hpp"

#include <cstdio>
#include <string>

namespace vpl::hardware::device_probe {
namespace {

std::string i2c_suffix(unsigned int address)
{
    if (address > 0x7f)
        return {};

    char suffix[6] = {};
    std::snprintf(suffix, sizeof(suffix), "-%04x", address);
    return suffix;
}

bool entry_matches_i2c_address(const std::string &entry, unsigned int address)
{
    const std::string suffix = i2c_suffix(address);
    return !suffix.empty() && entry.size() >= suffix.size() &&
           entry.compare(entry.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

bool input_name_contains(const std::string &needle)
{
    for (const std::string &entry : paths::children("/sys/class/input")) {
        const std::string name = paths::read("/sys/class/input/" + entry + "/device/name");
        if (name.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool i2c_address_enumerated(unsigned int address)
{
    for (const std::string &entry : paths::children("/sys/bus/i2c/devices")) {
        if (entry_matches_i2c_address(entry, address))
            return true;
    }
    return false;
}

bool i2c_address_bound_to_driver(unsigned int address, const std::string &driver)
{
    for (const std::string &entry : paths::children("/sys/bus/i2c/devices")) {
        if (!entry_matches_i2c_address(entry, address))
            continue;

        const std::string base = "/sys/bus/i2c/devices/" + entry;
        if (!paths::exists(base + "/driver"))
            continue;

        const std::string modalias = paths::read(base + "/modalias");
        if (driver.empty() || modalias.find(driver) != std::string::npos)
            return true;
    }
    return false;
}

bool class_has_entries(const std::string &directory)
{
    return !paths::children(directory).empty();
}

}  // namespace vpl::hardware::device_probe

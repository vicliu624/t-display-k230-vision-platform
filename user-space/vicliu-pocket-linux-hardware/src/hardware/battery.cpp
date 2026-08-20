#include "battery.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace vpl::hardware {
namespace {

std::optional<long> micro_to_milli(const std::string &path)
{
    const std::optional<long> value = paths::read_long(path);
    if (!value)
        return std::nullopt;
    return *value / 1000;
}

bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool matches_battery(const std::string &supply)
{
    const std::string type = paths::read("/sys/class/power_supply/" + supply + "/type");
    return type == "Battery" || starts_with(supply, "battery");
}

bool matches_charger(const std::string &supply)
{
    const std::string type = paths::read("/sys/class/power_supply/" + supply + "/type");
    return type == "USB" || type == "Mains" || type == "USB_DCP" ||
           type == "USB_CDP" || type == "USB_ACA" || starts_with(supply, "charger");
}

}  // namespace

BatteryReading read_battery_supply()
{
    for (const std::string &supply : paths::children("/sys/class/power_supply")) {
        if (!matches_battery(supply))
            continue;
        const std::string base = "/sys/class/power_supply/" + supply + "/";
        const std::optional<long> capacity = paths::read_long(base + "capacity");
        if (!capacity || *capacity < 0 || *capacity > 100)
            continue;
        const std::optional<long> voltage = micro_to_milli(base + "voltage_now");
        const std::optional<long> current = micro_to_milli(base + "current_now");
        const std::optional<long> temperature = paths::read_long(base + "temp");
        return {true, supply, *capacity, voltage.value_or(0), current.value_or(0),
                temperature.value_or(0) / 10};
    }
    return {};
}

ChargerReading read_charger_supply()
{
    for (const std::string &supply : paths::children("/sys/class/power_supply")) {
        if (!matches_charger(supply))
            continue;
        const std::string base = "/sys/class/power_supply/" + supply + "/";
        const std::string online = paths::read(base + "online");
        const std::string status = paths::read(base + "status");
        return {true, supply, online == "1" || status == "Charging" || status == "Full", status};
    }
    return {};
}

}  // namespace vpl::hardware

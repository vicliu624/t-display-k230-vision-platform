#include "contract.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>

namespace vpl::hardware {
namespace {

std::optional<std::string> backlight(const std::string &name)
{
    const std::string requested = name == "display-brightness" ? "display" : "keyboard";
    for (const std::string &entry : paths::children("/sys/class/backlight")) {
        if (entry.find(requested) != std::string::npos)
            return entry;
    }
    return std::nullopt;
}

int parse_percent(const std::string &value, long *percent)
{
    if (value.empty())
        return 64;

    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end != value.c_str() + value.size() || parsed < 0 || parsed > 100)
        return 64;
    *percent = parsed;
    return 0;
}

}  // namespace

int get_control(const std::string &name, std::string *value)
{
    const std::optional<std::string> device = backlight(name);
    if (!device)
        return 69;
    const std::string base = "/sys/class/backlight/" + *device;
    const std::optional<long> current = paths::read_long(base + "/brightness");
    const std::optional<long> maximum = paths::read_long(base + "/max_brightness");
    if (!current || !maximum || *maximum <= 0)
        return 70;
    *value = std::to_string(std::clamp((*current * 100 + *maximum / 2) / *maximum, 0L, 100L));
    return 0;
}

int set_control(const std::string &name, const std::string &value)
{
    long percent = 0;
    const int parsed = parse_percent(value, &percent);
    if (parsed != 0)
        return parsed;
    const std::optional<std::string> device = backlight(name);
    if (!device)
        return 69;
    const std::string base = "/sys/class/backlight/" + *device;
    const std::optional<long> maximum = paths::read_long(base + "/max_brightness");
    if (!maximum || *maximum <= 0)
        return 70;
    const long requested = std::clamp((percent * *maximum + 50) / 100, 0L, *maximum);
    return paths::write(base + "/brightness", std::to_string(requested)) ? 0 : 74;
}

}  // namespace vpl::hardware

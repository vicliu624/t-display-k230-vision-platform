// Exercise the production control implementation with isolated sysfs/MMIO fakes.
// No access to the test host's /dev/mem, PWM or backlight devices is permitted.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <optional>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include "paths.hpp"

namespace fixture {
std::map<std::string, std::string> files;
std::vector<std::pair<std::string, std::string>> writes;
alignas(uint32_t) std::array<uint8_t, 4096> iomux;
bool class_present, controller_present, exported, reject_open, reject_map;
std::string fail_path, ignore_path;
constexpr char channel[] = "/sys/class/pwm/pwmchip3/pwm1";
int open_mem(const char *path, int flags)
{
    assert(std::string(path) == "/dev/mem");
    assert(flags == (O_RDWR | O_SYNC | O_CLOEXEC));
    return reject_open ? -1 : 123;
}
void *map_mem(void *, size_t size, int prot, int flags, int fd, off_t offset)
{
    assert(size == 4096 && prot == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED);
    assert(fd == 123 && offset == 0x91105000);
    return reject_map ? MAP_FAILED : iomux.data();
}
int close_mem(int fd) { assert(fd == 123); return 0; }
int unmap_mem(void *address, size_t size)
{
    assert(address == iomux.data() && size == 4096);
    return 0;
}
} // namespace fixture

#define open fixture::open_mem
#define mmap fixture::map_mem
#define close fixture::close_mem
#define munmap fixture::unmap_mem
#include "controls.cpp"
#undef open
#undef mmap
#undef close
#undef munmap

namespace vpl::hardware::paths {
bool exists(const std::string &path)
{
    return (path == fixture::channel && fixture::exported) || fixture::files.count(path);
}
std::string read(const std::string &path)
{
    const auto it = fixture::files.find(path);
    return it == fixture::files.end() ? "" : it->second;
}
std::optional<long> read_long(const std::string &path)
{
    const auto value = read(path);
    if (value.empty()) return std::nullopt;
    return std::stol(value);
}
std::vector<std::string> children(const std::string &path)
{
    if (path == "/sys/class/backlight")
        return fixture::class_present ? std::vector<std::string>{"display-backlight", "keyboard-backlight"}
                                      : std::vector<std::string>{"display-backlight"};
    if (path == "/sys/class/pwm" && fixture::controller_present) return {"pwmchip3"};
    return {};
}
bool write(const std::string &path, const std::string &value)
{
    fixture::writes.emplace_back(path, value);
    if (path == fixture::fail_path) return false;
    if (path == fixture::ignore_path) return true; // buffered write reported success, sysfs rejected it
    if (path == "/sys/bus/platform/drivers/pwm-backlight/unbind") {
        assert(value == "keyboard-backlight");
        fixture::class_present = false;
    } else if (path == "/sys/class/pwm/pwmchip3/export") {
        assert(value == "1" && !fixture::class_present);
        fixture::exported = true;
    } else {
        assert(path.find(fixture::channel) == 0 || path == "/sys/class/backlight/display-backlight/brightness");
        fixture::files[path] = value;
    }
    return true;
}
} // namespace vpl::hardware::paths

int main()
{
    using namespace vpl::hardware;
    using namespace fixture;
    const auto reset = [] {
        files.clear(); writes.clear(); iomux.fill(0);
        class_present = controller_present = true;
        exported = reject_open = reject_map = false;
        fail_path.clear(); ignore_path.clear();
        keyboard_backlight_percent_cache.reset();
        files["/sys/class/pwm/pwmchip3/device/of_node/name"] = std::string("pwm3_5\0", 7);
        files["/sys/class/backlight/keyboard-backlight/brightness"] = "0";
        files["/sys/class/backlight/keyboard-backlight/max_brightness"] = "10";
        files["/sys/class/backlight/display-backlight/brightness"] = "50";
        files["/sys/class/backlight/display-backlight/max_brightness"] = "200";
        // Legacy ascending DT, keyboard class brightness 0 = electrically on.
        files["/sys/firmware/devicetree/base/keyboard-backlight/brightness-levels"] =
            std::string("\0\0\0\0\0\0\0\xff", 8);
    };
    reset();
    assert(initialise_keyboard_backlight());
    assert(!class_present && exported);
    for (size_t i = 0; i < iomux.size(); ++i) {
        const uint8_t expected = i == 208 ? 0x91 : i == 209 ? 0x11 : 0;
        assert(iomux[i] == expected); // only IO52, no camera/AI/display pin writes
    }
    std::string value;
    assert(get_control("keyboard-brightness", &value) == 0 && value == "100");
    for (const auto &[request, expected] : std::vector<std::pair<std::string, long>>{
             {"0", 0}, {"1", 33}, {"33", 33}, {"66", 33}, {"67", 100}, {"100", 100}}) {
        writes.clear();
        assert(set_control("keyboard-brightness", request) == 0);
        assert(get_control("keyboard-brightness", &value) == 0 && value == std::to_string(expected));
        assert(files[std::string(channel) + "/period"] == "1000000");
        assert(files[std::string(channel) + "/duty_cycle"] == std::to_string(10000 * (100 - expected)));
        assert(files[std::string(channel) + "/enable"] == "1");
        assert(writes.size() == 6 && writes[0].second == "0" && writes[5].second == "1");
    }
    // External CLI changes are visible instead of returning the daemon's cache.
    files[std::string(channel) + "/duty_cycle"] = "670000";
    assert(get_control("keyboard-brightness", &value) == 0 && value == "33");
    keyboard_backlight_percent_cache.reset();
    assert(initialise_keyboard_backlight());
    assert(get_control("keyboard-brightness", &value) == 0 && value == "33");
    writes.clear();
    for (const std::string bad : {"", "-1", "101", "NaN", "33x", "9999999999999999999999"})
        assert(set_control("keyboard-brightness", bad) == 64);
    assert(writes.empty());
    assert(get_control("display-brightness", &value) == 0 && value == "25");
    assert(set_control("display-brightness", "75") == 0);
    assert(writes.size() == 1 && writes[0].second == "150");

    for (const char *leaf : {"enable", "duty_cycle", "period", "polarity"}) {
        reset(); assert(initialise_keyboard_backlight());
        fail_path = std::string(channel) + "/" + leaf;
        assert(set_control("keyboard-brightness", "33") == 74);
    }
    reset(); assert(initialise_keyboard_backlight());
    ignore_path = std::string(channel) + "/duty_cycle";
    assert(set_control("keyboard-brightness", "33") == 74);
    reset(); reject_open = true;
    assert(set_control("keyboard-brightness", "33") == 73 && writes.empty());
    reset(); reject_map = true;
    assert(set_control("keyboard-brightness", "33") == 73 && writes.empty());
    reset(); controller_present = false;
    assert(set_control("keyboard-brightness", "33") == 73);
    reset(); fail_path = "/sys/bus/platform/drivers/pwm-backlight/unbind";
    assert(set_control("keyboard-brightness", "33") == 73 && !exported);
    reset(); fail_path = "/sys/class/pwm/pwmchip3/export";
    assert(set_control("keyboard-brightness", "33") == 73 && !exported);
    reset();
    files["/sys/firmware/devicetree/base/keyboard-backlight/brightness-levels"] =
        std::string("\0\0\0\xff\0\0\0\0", 8);
    assert(initialise_keyboard_backlight());
    assert(get_control("keyboard-brightness", &value) == 0 && value == "0");
    files[std::string(channel) + "/enable"] = "0";
    assert(get_control("keyboard-brightness", &value) != 0);
    puts("keyboard-backlight: PASS production controls, IO52-only mapping, presets, restart/readback, failures and display isolation");
}

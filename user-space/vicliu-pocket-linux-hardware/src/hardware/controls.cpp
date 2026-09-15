#include "contract.hpp"

#include "paths.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <optional>
#include <string>

namespace vpl::hardware {
namespace {

constexpr off_t kK230IomuxBase = 0x91105000;
constexpr std::size_t kK230IomuxSize = 0x1000;
constexpr std::size_t kKeyboardBacklightIomuxOffset = 52U * sizeof(uint32_t);
constexpr uint32_t kKeyboardBacklightPwm4MuxValue = 0x00001191U;
constexpr char kKeyboardPwmControllerNode[] = "pwm3_5";
constexpr char kKeyboardPwmChannelName[] = "pwm1";
constexpr long kKeyboardPwmPeriodNs = 1000000L;

std::optional<long> keyboard_backlight_percent_cache;

std::optional<std::string> backlight(const std::string &name)
{
    const std::string requested = name == "display-brightness" ? "display" : "keyboard";
    for (const std::string &entry : paths::children("/sys/class/backlight")) {
        if (entry.find(requested) != std::string::npos)
            return entry;
    }
    return std::nullopt;
}

bool keyboard_backlight_uses_legacy_ascending_levels()
{
    // The pre-fix DTB published an ascending brightness table while PWM4 was
    // inverted.  It made the class-device brightness number the inverse of
    // the electrical brightness.  Keep the root service compatible with an
    // already-flashed image, but let the corrected DTB use Linux's normal
    // 0%=off / 100%=on contract without a second inversion.
    const std::string levels =
        paths::read("/sys/firmware/devicetree/base/keyboard-backlight/brightness-levels");
    if (levels.size() < 2U * sizeof(uint32_t))
        return false;

    const auto read_big_endian_u32 = [&levels](std::size_t offset) {
        const auto *const bytes = reinterpret_cast<const unsigned char *>(levels.data() + offset);
        return (static_cast<uint32_t>(bytes[0]) << 24U) |
            (static_cast<uint32_t>(bytes[1]) << 16U) |
            (static_cast<uint32_t>(bytes[2]) << 8U) |
            static_cast<uint32_t>(bytes[3]);
    };
    return read_big_endian_u32(0) <
        read_big_endian_u32(levels.size() - sizeof(uint32_t));
}

std::optional<long> keyboard_backlight_percent_from_class()
{
    const std::optional<std::string> device = backlight("keyboard-backlight");
    if (!device)
        return std::nullopt;
    const std::string base = "/sys/class/backlight/" + *device;
    const std::optional<long> current = paths::read_long(base + "/brightness");
    const std::optional<long> maximum = paths::read_long(base + "/max_brightness");
    if (!current || !maximum || *maximum <= 0)
        return std::nullopt;
    long percent = std::clamp((*current * 100 + *maximum / 2) / *maximum, 0L, 100L);
    if (keyboard_backlight_uses_legacy_ascending_levels())
        percent = 100L - percent;
    return percent;
}

std::optional<std::string> keyboard_pwm_controller()
{
    for (const std::string &entry : paths::children("/sys/class/pwm")) {
        const std::string node =
            paths::read("/sys/class/pwm/" + entry + "/device/of_node/name");
        if (node.size() >= sizeof(kKeyboardPwmControllerNode) - 1U &&
            node.compare(0, sizeof(kKeyboardPwmControllerNode) - 1U,
                         kKeyboardPwmControllerNode) == 0) {
            return "/sys/class/pwm/" + entry;
        }
    }
    return std::nullopt;
}

std::optional<long> keyboard_backlight_percent_from_pwm()
{
    const std::optional<std::string> controller = keyboard_pwm_controller();
    if (!controller)
        return std::nullopt;
    const std::string channel = *controller + "/" + kKeyboardPwmChannelName;
    if (paths::read(channel + "/enable") != "1")
        return std::nullopt;
    const std::optional<long> period = paths::read_long(channel + "/period");
    const std::optional<long> duty = paths::read_long(channel + "/duty_cycle");
    if (!period || !duty || *period <= 0 || *duty < 0 || *duty > *period)
        return std::nullopt;
    const bool inverted = paths::read(channel + "/polarity") == "inversed";
    const long active = inverted ? *period - *duty : *duty;
    return std::clamp((active * 100 + *period / 2) / *period, 0L, 100L);
}

bool ensure_keyboard_pwm_channel(std::string *channel)
{
    const std::optional<std::string> controller = keyboard_pwm_controller();
    if (!controller)
        return false;
    const std::string requested = *controller + "/" + kKeyboardPwmChannelName;
    if (!paths::exists(requested) &&
        !paths::write(*controller + "/export", "1") && !paths::exists(requested)) {
        return false;
    }
    if (!paths::exists(requested))
        return false;
    *channel = requested;
    return true;
}

bool configure_keyboard_backlight_pwm(long percent)
{
    std::string channel;
    if (!ensure_keyboard_pwm_channel(&channel))
        return false;
    percent = std::clamp(percent, 0L, 100L);
    const long duty = kKeyboardPwmPeriodNs * (100L - percent) / 100L;

    // PWM4 accepts only inverted polarity on this board.  Reconfigure while
    // disabled, avoiding an invalid duty > period transition. The vendor
    // driver implements disable as duty=0; do not treat it as electrical off.
    return paths::write(channel + "/enable", "0") &&
        paths::write(channel + "/duty_cycle", "0") &&
        paths::write(channel + "/period", std::to_string(kKeyboardPwmPeriodNs)) &&
        paths::write(channel + "/polarity", "inversed") &&
        paths::write(channel + "/duty_cycle", std::to_string(duty)) &&
        paths::write(channel + "/enable", "1") &&
        paths::read_long(channel + "/period") == kKeyboardPwmPeriodNs &&
        paths::read_long(channel + "/duty_cycle") == duty &&
        paths::read(channel + "/polarity") == "inversed" &&
        paths::read(channel + "/enable") == "1";
}

long keyboard_backlight_preset_percent(long percent)
{
    // The keyboard LED rail only has three perceptually distinct states on
    // target.  Keep the provider contract truthful even when a legacy CLI or
    // a future client sends an arbitrary 0..100 value.
    if (percent <= 0)
        return 0;
    if (percent < 67)
        return 33;
    return 100;
}

bool unbind_generic_keyboard_backlight()
{
    const std::optional<std::string> device = backlight("keyboard-backlight");
    if (!device)
        return true;
    return paths::write("/sys/bus/platform/drivers/pwm-backlight/unbind", *device);
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

bool prepare_keyboard_backlight_route()
{
    const int descriptor = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (descriptor < 0)
        return false;
    void *const mapping = mmap(nullptr, kK230IomuxSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                               descriptor, kK230IomuxBase);
    (void)close(descriptor);
    if (mapping == MAP_FAILED)
        return false;

    volatile auto *const register_base = static_cast<volatile uint8_t *>(mapping);
    volatile auto *const pin = reinterpret_cast<volatile uint32_t *>(
        register_base + kKeyboardBacklightIomuxOffset);
    *pin = kKeyboardBacklightPwm4MuxValue;
    const bool selected = *pin == kKeyboardBacklightPwm4MuxValue;
    (void)munmap(mapping, kK230IomuxSize);
    return selected;
}

bool initialise_keyboard_backlight()
{
    // Retain the user's active level while replacing the generic 20 kHz
    // pwm-backlight consumer with this board-specific 1 kHz driver path.
    // The latter is the frequency at which the physical keyboard LEDs showed
    // distinct levels during on-device acceptance.
    std::optional<long> percent = keyboard_backlight_percent_from_class();
    if (!percent)
        percent = keyboard_backlight_percent_from_pwm();
    if (!percent)
        percent = 100L;
    *percent = keyboard_backlight_preset_percent(*percent);
    if (!prepare_keyboard_backlight_route() || !unbind_generic_keyboard_backlight() ||
        !configure_keyboard_backlight_pwm(*percent)) {
        return false;
    }
    keyboard_backlight_percent_cache = *percent;
    return true;
}

int get_control(const std::string &name, std::string *value)
{
    if (name != "display-brightness") {
        // Read the active waveform, not a per-process cache: CLI changes and
        // daemon restarts must be visible to the Quick Settings publisher.
        const std::optional<long> percent = keyboard_backlight_percent_from_pwm();
        if (percent) {
            *value = std::to_string(*percent);
            return 0;
        }
        if (keyboard_backlight_percent_cache)
            return 70;
    }
    const std::optional<std::string> device = backlight(name);
    if (!device)
        return 69;
    const std::string base = "/sys/class/backlight/" + *device;
    const std::optional<long> current = paths::read_long(base + "/brightness");
    const std::optional<long> maximum = paths::read_long(base + "/max_brightness");
    if (!current || !maximum || *maximum <= 0)
        return 70;
    long percent = std::clamp((*current * 100 + *maximum / 2) / *maximum, 0L, 100L);
    if (name != "display-brightness" && keyboard_backlight_uses_legacy_ascending_levels())
        percent = 100L - percent;
    *value = std::to_string(percent);
    return 0;
}

int set_control(const std::string &name, const std::string &value)
{
    long percent = 0;
    const int parsed = parse_percent(value, &percent);
    if (parsed != 0)
        return parsed;
    if (name != "display-brightness") {
        percent = keyboard_backlight_preset_percent(percent);
        if (!keyboard_backlight_percent_cache && !initialise_keyboard_backlight())
            return 73;
        if (!configure_keyboard_backlight_pwm(percent))
            return 74;
        keyboard_backlight_percent_cache = percent;
        return 0;
    }
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

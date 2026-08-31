#include "contract.hpp"

#include "battery.hpp"
#include "dock.hpp"
#include "network.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>

namespace vpl::hardware {
namespace {

void put(State *state, const std::string &key, bool value)
{
    (*state)[key] = value ? "1" : "0";
}

bool directory_has(const std::string &directory, const std::string &needle)
{
    for (const std::string &entry : paths::children(directory)) {
        if (entry.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

bool any_backlight(const std::string &needle)
{
    return directory_has("/sys/class/backlight", needle);
}

bool any_alsa_card()
{
    const std::string cards = paths::read("/proc/asound/cards");
    return !cards.empty() && cards.find("no soundcards") == std::string::npos;
}

std::string external_i2s_route()
{
    if (!paths::executable("/usr/bin/amixer"))
        return {};
    int status = 127;
    const std::string control = paths::run_capture(
        {"/usr/bin/amixer", "cget", "name=External I2S Output Switch"}, &status);
    if (status != 0)
        return {};
    if (control.find("values=on") != std::string::npos ||
        control.find("values=1") != std::string::npos)
        return "external";
    if (control.find("values=off") != std::string::npos ||
        control.find("values=0") != std::string::npos)
        return "internal";
    return "unknown";
}

std::string pcm_playback_state()
{
    if (!paths::executable("/usr/bin/amixer"))
        return {};
    int status = 127;
    const std::string control = paths::run_capture(
        {"/usr/bin/amixer", "-c", "0", "sget", "PCM"}, &status);
    return status == 0 ? control : std::string {};
}

std::optional<int> pcm_volume_percent(const std::string &playback)
{
    for (std::size_t bracket = playback.find('['); bracket != std::string::npos;
         bracket = playback.find('[', bracket + 1)) {
        std::size_t cursor = bracket + 1;
        if (cursor >= playback.size() || playback[cursor] < '0' || playback[cursor] > '9')
            continue;
        int percent = 0;
        while (cursor < playback.size() && playback[cursor] >= '0' && playback[cursor] <= '9') {
            percent = percent * 10 + (playback[cursor] - '0');
            if (percent > 100)
                break;
            ++cursor;
        }
        if (percent <= 100 && cursor < playback.size() && playback[cursor] == '%')
            return percent;
    }
    return std::nullopt;
}

std::optional<bool> pcm_muted(const std::string &playback)
{
    if (playback.find("[off]") != std::string::npos)
        return true;
    if (playback.find("[on]") != std::string::npos)
        return false;
    return std::nullopt;
}

bool any_camera()
{
    for (const std::string &entry : paths::children("/sys/class/video4linux")) {
        const std::string name = paths::read("/sys/class/video4linux/" + entry + "/name");
        if (!name.empty() && name.find("mvx") == std::string::npos)
            return true;
    }
    return false;
}

std::string backlight_percent(std::initializer_list<const char *> needles)
{
    for (const std::string &entry : paths::children("/sys/class/backlight")) {
        bool matches = false;
        for (const char *needle : needles) {
            if (entry.find(needle) != std::string::npos) {
                matches = true;
                break;
            }
        }
        if (!matches)
            continue;
        const std::optional<long> current = paths::read_long("/sys/class/backlight/" + entry + "/brightness");
        const std::optional<long> maximum = paths::read_long("/sys/class/backlight/" + entry + "/max_brightness");
        if (current && maximum && *maximum > 0)
            return std::to_string(std::clamp((*current * 100 + *maximum / 2) / *maximum, 0L, 100L));
    }
    return {};
}

bool cpu_is_online(const std::string &online, unsigned int cpu)
{
    std::size_t offset = 0;
    while (offset < online.size()) {
        const std::size_t separator = online.find(',', offset);
        const std::string range = online.substr(offset, separator - offset);
        char *end = nullptr;
        errno = 0;
        const long first = std::strtol(range.c_str(), &end, 10);
        if (errno != 0 || end == range.c_str() || first < 0)
            return false;
        long last = first;
        if (*end == '-') {
            char *last_end = nullptr;
            errno = 0;
            last = std::strtol(end + 1, &last_end, 10);
            if (errno != 0 || last_end == end + 1 || *last_end != '\0' || last < first)
                return false;
        } else if (*end != '\0') {
            return false;
        }
        if (static_cast<long>(cpu) >= first && static_cast<long>(cpu) <= last)
            return true;
        if (separator == std::string::npos)
            break;
        offset = separator + 1;
    }
    return false;
}

unsigned int cpu_count(const std::string &online)
{
    unsigned int count = 0;
    std::size_t offset = 0;
    while (offset < online.size()) {
        const std::size_t separator = online.find(',', offset);
        const std::string range = online.substr(offset, separator - offset);
        char *end = nullptr;
        errno = 0;
        const long first = std::strtol(range.c_str(), &end, 10);
        if (errno != 0 || end == range.c_str() || first < 0)
            return 0;
        long last = first;
        if (*end == '-') {
            char *last_end = nullptr;
            errno = 0;
            last = std::strtol(end + 1, &last_end, 10);
            if (errno != 0 || last_end == end + 1 || *last_end != '\0' || last < first)
                return 0;
        } else if (*end != '\0') {
            return 0;
        }
        count += static_cast<unsigned int>(last - first + 1);
        if (separator == std::string::npos)
            break;
        offset = separator + 1;
    }
    return count;
}

bool acceptance_passed(const std::string &name)
{
    return paths::exists("/run/vicliu-pocket-linux-hardware/acceptance/" + name + ".pass");
}

void transport(State *state, const std::string &name, bool present, bool driver, bool runtime)
{
    const bool available = present && driver && runtime;
    const bool functional = available && acceptance_passed(name);
    put(state, name + "_transport", present);
    put(state, name + "_driver", driver);
    put(state, name + "_runtime", runtime);
    put(state, name + "_functional", functional);
    put(state, name + "_available", available);
    put(state, name + "_active", functional);
    (*state)[name + "_acceptance"] = functional ? "passed" : (available ? "unverified" : "unavailable");
}

}  // namespace

State collect_state()
{
    State state;
    append_network_state(&state);
    const DockState dock = read_dock_state();
    append_dock_state(&state, dock);
    state["radio_resource_owner"] = "no-active-linux-profile";
    state["audio_fuel_gauge_resource_owner"] = "no-active-linux-profile";

    transport(&state, "bluetooth", false, false, false);
    put(&state, "bluetooth_requested", false);
    put(&state, "bluetooth_control_available", false);

    transport(&state, "gnss", false, false, false);
    const bool cellular_profile = paths::exists("/sys/devices/platform/radio-mux/profile");
    const bool cellular_uart = paths::exists("/dev/ttyS2");
    const bool cellular = cellular_profile && cellular_uart;
    transport(&state, "cellular", cellular, cellular, cellular);
    put(&state, "gnss_requested", false);
    put(&state, "gnss_control_available", false);
    put(&state, "cellular_requested", false);
    put(&state, "cellular_control_available", cellular);
    // The current profile has no registered modem service that can provide a
    // measured RSSI/RSRP value.  Keep the explicit unknown sentinel instead
    // of fabricating a signal level from UART availability.  A modem service
    // may publish a 0..100 measurement at this runtime path once it owns the
    // radio transport.
    const std::optional<long> cellular_signal =
        paths::read_long("/run/vicliu-pocket-linux-hardware/cellular/signal_percent");
    state["cellular_signal_percent"] =
        cellular && cellular_signal && *cellular_signal >= 0 && *cellular_signal <= 100
        ? std::to_string(*cellular_signal) : "-1";

    transport(&state, "lora", false, false, false);
    put(&state, "lora_requested", false);
    put(&state, "lora_control_available", false);

    const bool keyboard = dock.keyboard_input;
    const bool keyboard_backlight = any_backlight("keyboard");
    transport(&state, "keyboard", keyboard, keyboard, true);
    transport(&state, "keyboard_backlight", keyboard_backlight, keyboard_backlight,
              keyboard_backlight);
    state["keyboard_backlight_brightness_percent"] = backlight_percent({"keyboard"});

    const bool touch = directory_has("/sys/class/input", "input") &&
        (paths::exists("/sys/bus/i2c/devices/0-005d") || paths::exists("/sys/bus/i2c/devices/1-005d"));
    transport(&state, "touch", touch, touch, true);

    const bool display = paths::exists("/dev/dri/card0");
    const bool display_backlight = any_backlight("display") || any_backlight("panel");
    transport(&state, "display", display, display, true);
    put(&state, "display_brightness_control_available", display_backlight);
    state["display_brightness_percent"] = backlight_percent({"display", "panel"});

    const bool audio = any_alsa_card();
    const std::string pcm_playback = audio ? pcm_playback_state() : std::string {};
    const std::optional<int> volume_percent = pcm_volume_percent(pcm_playback);
    const std::optional<bool> muted = pcm_muted(pcm_playback);
    const std::string managed_route = audio ? external_i2s_route() : std::string {};
    const bool external_i2s = !managed_route.empty();
    const std::string route = external_i2s ? managed_route : "unavailable";
    transport(&state, "audio", audio, audio, audio);
    transport(&state, "microphone", audio, audio, audio);
    // An ALSA card only proves that the internal K230 codec registered. The
    // external speaker needs the managed ASoC route control and a policy that
    // selected its MAX98357A-compatible I2S path; audible output still needs
    // the explicit human acceptance record.
    transport(&state, "speaker", audio, external_i2s, route == "external");
    state["speaker_route"] = route;
    state["speaker_route_control"] = external_i2s ? "External I2S Output Switch" : "";
    state["speaker_amplifier_owner"] = external_i2s ? "asoc-k230-inno" : "";
    state["volume_percent"] = volume_percent ? std::to_string(*volume_percent) : "";
    put(&state, "muted", muted.value_or(false));
    put(&state, "audio_volume_control_available", volume_percent.has_value() && muted.has_value());

    const bool camera = any_camera();
    transport(&state, "camera", camera, camera, camera);

    const bool rtc = directory_has("/sys/class/rtc", "rtc");
    transport(&state, "rtc", rtc, rtc, rtc);

    const BatteryReading battery = read_battery_supply();
    const ChargerReading charger = read_charger_supply();
    const bool power_supply = !paths::children("/sys/class/power_supply").empty();
    const bool power = power_supply || battery.present || charger.present;
    transport(&state, "power", power, power, true);
    transport(&state, "battery", battery.present, battery.present, battery.present);
    put(&state, "charger_present", charger.present);
    put(&state, "charger_power_good", charger.power_good);
    state["charger_charge_state"] = charger.present ? charger.charge_state : "";
    if (battery.present) {
        state["battery_supply"] = battery.supply_name;
        state["battery_capacity_percent"] = std::to_string(battery.capacity_percent);
        state["battery_voltage_mv"] = std::to_string(battery.voltage_mv);
        state["battery_current_ma"] = std::to_string(battery.current_ma);
        state["battery_temperature_deci_celsius"] = std::to_string(battery.temperature_deci_celsius);
    }

    const bool gnne_driver = paths::exists("/sys/class/k230_gnne_class/k230-gnne");
    const bool ai2d_driver = paths::exists("/sys/class/k230_ai2d_class/k230-ai2d");
    const bool gnne_device = paths::exists("/dev/k230-gnne");
    const bool ai2d_device = paths::exists("/dev/k230-ai2d");
    const bool kpu_reference_runtime =
        paths::executable("/root/app/ai2d_kpu/ai2d_kpu.elf") &&
        paths::exists("/root/app/ai2d_kpu/test.kmodel") &&
        paths::exists("/root/app/ai2d_kpu/ai2d_input.bin") &&
        paths::exists("/root/app/ai2d_kpu/input.bin") &&
        paths::exists("/root/app/ai2d_kpu/result.bin");
    transport(&state, "kpu", gnne_device && ai2d_device, gnne_driver && ai2d_driver,
              kpu_reference_runtime);
    put(&state, "kpu_gnne_device", gnne_device);
    put(&state, "kpu_ai2d_device", ai2d_device);
    put(&state, "kpu_kernel_ready", gnne_driver && ai2d_driver && gnne_device && ai2d_device);
    put(&state, "kpu_reference_runtime_available", kpu_reference_runtime);
    state["kpu_runtime"] = kpu_reference_runtime ? "nncase-k230" : "";
    const bool kpu_acceptance_service_active =
        paths::service_active("tdvp-kpu-acceptance.service");
    const bool kpu_acceptance_passed =
        paths::exists("/run/vicliu-pocket-linux-hardware/kpu-acceptance.pass");
    const bool kpu_acceptance_skipped =
        paths::exists("/run/vicliu-pocket-linux-hardware/kpu-acceptance.skipped");
    put(&state, "kpu_acceptance_service_active", kpu_acceptance_service_active);
    put(&state, "kpu_acceptance_passed", kpu_acceptance_passed);
    put(&state, "kpu_acceptance_skipped", kpu_acceptance_skipped);
    if (!gnne_driver || !ai2d_driver || !gnne_device || !ai2d_device) {
        state["kpu_acceptance_state"] = "kernel-unavailable";
    } else if (!kpu_reference_runtime) {
        state["kpu_acceptance_state"] = "runtime-unavailable";
    } else if (kpu_acceptance_passed) {
        state["kpu_acceptance_state"] = "passed";
    } else if (kpu_acceptance_skipped) {
        state["kpu_acceptance_state"] = "skipped";
    } else if (kpu_acceptance_service_active) {
        state["kpu_acceptance_state"] = "running";
    } else {
        state["kpu_acceptance_state"] = "pending-or-failed";
    }
    if (kpu_acceptance_passed) {
        state["kpu_acceptance"] = "passed";
        put(&state, "kpu_functional", true);
        put(&state, "kpu_active", true);
    }

    const std::string online = paths::read("/sys/devices/system/cpu/online");
    state["cpu_physical_core_count"] = "2";
    state["soc_cpu_cores"] = state["cpu_physical_core_count"];
    state["cpu_online"] = online.empty() ? "unknown" : online;
    state["linux_schedulable_cpu_count"] =
        online.empty() ? "0" : std::to_string(cpu_count(online));
    state["cpu_count"] = state["linux_schedulable_cpu_count"];
    put(&state, "cpu0_linux_online", !online.empty() && cpu_is_online(online, 0));
    put(&state, "cpu1_physical_present", true);
    state["cpu1_execution_model"] = "unprovisioned";
    state["secondary_cpu_linux_managed"] = "0";
    put(&state, "cpu1_firmware_lifecycle_available", false);
    put(&state, "cpu1_coprocessor_available", false);
    put(&state, "cpu1_coprocessor_active", false);
    put(&state, "cpu1_asr_offload_available", false);
    return state;
}

int initialise()
{
    // Kernel probe owns the device model. This process deliberately does not
    // change FPIOA, GPIO, I2C, SPI, UART, PWM, or power state at boot.
    return 0;
}

}  // namespace vpl::hardware

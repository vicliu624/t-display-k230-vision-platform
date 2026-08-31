#include "quick_settings_service.hpp"

#include "nrf9151.hpp"
#include "paths.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <sstream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace vpl::hardware {
namespace {

constexpr char kRuntimeDirectory[] = "/run/vicliu-pocket-linux-hardware";
constexpr char kSocketPath[] = "/run/vicliu-pocket-linux-hardware/quick-settings.sock";
constexpr char kRadioProfilePath[] = "/sys/devices/platform/radio-mux/profile";
constexpr char kLoraStatePath[] = "/sys/devices/platform/radio-mux/lora_state";
constexpr std::size_t kMaximumRequestBytes = 512;

std::string trim(std::string value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_percent(const std::string &value)
{
    if (value.empty())
        return false;
    long result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9')
            return false;
        result = result * 10 + (character - '0');
        if (result > 100)
            return false;
    }
    return true;
}

bool is_keyboard_attached(const State &state)
{
    const auto found = state.find("dock_profile");
    return found != state.end() && found->second == "attached";
}

bool lora_is_enabled()
{
    return paths::read(kRadioProfilePath) == "lora" && paths::read(kLoraStatePath) == "on";
}

bool lora_control_available()
{
    return paths::exists(kRadioProfilePath) && paths::exists(kLoraStatePath) &&
           paths::exists("/dev/spidev0.0");
}

std::string serialize_ok(const State &state)
{
    std::ostringstream result;
    result << "result=ok\n";
    for (const auto &[key, value] : state)
        result << key << '=' << value << '\n';
    return result.str();
}

std::string serialize_error(const std::string &error)
{
    return "result=error\nerror=" + error + "\n";
}

}  // namespace

QuickSettingsService::~QuickSettingsService()
{
    stop_gnss_startup();
    stop_lte_probe();
    if (listener_ >= 0)
        close(listener_);
    (void)unlink(kSocketPath);
}

bool QuickSettingsService::initialise()
{
    const passwd *desktop = getpwnam("tdvp");
    const group *desktop_group = getgrnam("tdvp");
    if (desktop == nullptr || desktop_group == nullptr)
        return false;
    desktop_uid_ = desktop->pw_uid;
    desktop_gid_ = desktop_group->gr_gid;

    if (mkdir(kRuntimeDirectory, 0755) != 0 && errno != EEXIST)
        return false;
    (void)unlink(kSocketPath);
    listener_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener_ < 0)
        return false;

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (std::strlen(kSocketPath) >= sizeof(address.sun_path))
        return false;
    std::strcpy(address.sun_path, kSocketPath);
    if (bind(listener_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        chmod(kSocketPath, 0660) != 0 || chown(kSocketPath, 0, desktop_gid_) != 0 ||
        listen(listener_, 4) != 0) {
        close(listener_);
        listener_ = -1;
        (void)unlink(kSocketPath);
        return false;
    }
    return true;
}

void QuickSettingsService::stop_lte_probe()
{
    if (lte_probe_pid_ <= 0)
        return;
    (void)kill(lte_probe_pid_, SIGTERM);
    while (waitpid(lte_probe_pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
    lte_probe_pid_ = -1;
}

void QuickSettingsService::stop_gnss_startup()
{
    if (gnss_start_pid_ > 0) {
        (void)kill(gnss_start_pid_, SIGTERM);
        while (waitpid(gnss_start_pid_, nullptr, 0) < 0 && errno == EINTR) {
        }
        gnss_start_pid_ = -1;
    }
    gps_state_ = "off";
}

void QuickSettingsService::update_gnss_startup()
{
    if (gnss_start_pid_ <= 0)
        return;
    int status = 0;
    const pid_t complete = waitpid(gnss_start_pid_, &status, WNOHANG);
    if (complete != gnss_start_pid_)
        return;
    gnss_start_pid_ = -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        // The modem is activated; position acquisition is asynchronous and a
        // later GNSS/NMEA service may upgrade this to "fix" after a measured
        // sentence rather than this UI fabricating a location.
        gps_state_ = "searching";
    } else {
        gnss_requested_ = false;
        gps_state_ = "fault";
    }
}

void QuickSettingsService::update_lte_detection(const State &state)
{
    if (!is_keyboard_attached(state)) {
        stop_lte_probe();
        stop_gnss_startup();
        lte_sku_state_ = "detached";
        gnss_requested_ = false;
        return;
    }

    if (lte_probe_pid_ > 0) {
        int status = 0;
        const pid_t complete = waitpid(lte_probe_pid_, &status, WNOHANG);
        if (complete == lte_probe_pid_) {
            lte_probe_pid_ = -1;
            lte_sku_state_ = WIFEXITED(status) && WEXITSTATUS(status) == 0 ?
                "lte-present" : "lte-not-detected";
        }
        return;
    }

    // Do not interrupt an enabled LoRa workload just to identify an optional
    // keyboard modem.  Once LoRa is off, the one-off probe may safely select
    // the nRF9151 profile and restores the original profile itself.
    if (lora_is_enabled()) {
        if (lte_sku_state_ == "detached")
            lte_sku_state_ = "lte-pending";
        return;
    }
    if (lte_sku_state_ != "detached" && lte_sku_state_ != "lte-pending")
        return;

    const pid_t child = fork();
    if (child < 0) {
        lte_sku_state_ = "lte-fault";
        return;
    }
    if (child == 0) {
        const Nrf9151ProbeResult result = probe_nrf9151();
        _exit(result.response.find("OK") != std::string::npos ? 0 : 1);
    }
    lte_probe_pid_ = child;
    lte_sku_state_ = "lte-probing";
}

bool QuickSettingsService::lte_present() const
{
    return lte_sku_state_ == "lte-present";
}

void QuickSettingsService::refresh(State *state)
{
    if (state == nullptr)
        return;
    update_lte_detection(*state);
    update_gnss_startup();
    (*state)["dock_nrf9151_sku_state"] = lte_sku_state_;

    const bool lora_available = is_keyboard_attached(*state) && lora_control_available();
    const bool lora_enabled = lora_available && lora_is_enabled();
    (*state)["lora_available"] = lora_available ? "1" : "0";
    (*state)["lora_control_available"] = lora_available ? "1" : "0";
    (*state)["lora_enabled"] = lora_enabled ? "1" : "0";
    (*state)["lora_requested"] = lora_enabled ? "1" : "0";
    (*state)["radio_profile"] = paths::read(kRadioProfilePath);

    const bool gps_available = lte_present();
    (*state)["gps_available"] = gps_available ? "1" : "0";
    (*state)["gnss_control_available"] = gps_available ? "1" : "0";
    (*state)["gnss_requested"] = gnss_requested_ ? "1" : "0";
    (*state)["gps_enabled"] = gnss_requested_ ? "1" : "0";
    (*state)["gps_state"] = !gps_available || !gnss_requested_ ? "off" : gps_state_;
    (*state)["gnss_state"] = (*state)["gps_state"];
    (*state)["radio_resource_owner"] = lora_enabled ? "lr2021" :
        (gnss_requested_ ? "nrf9151-gnss" : "no-active-linux-profile");
    last_state_ = *state;
}

bool QuickSettingsService::select_radio_profile(const std::string &profile, std::string *error)
{
    if (profile != "lora" && profile != "nrf9151") {
        *error = "unsupported radio profile";
        return false;
    }
    if (!paths::exists(kRadioProfilePath)) {
        *error = "K230 radio profile selector is unavailable";
        return false;
    }
    if (profile == "nrf9151" && !lte_present()) {
        *error = "the attached keyboard does not have verified LTE/GPS hardware";
        return false;
    }
    if (!paths::write(kLoraStatePath, "off")) {
        *error = "could not power down LoRa before changing radio profile";
        return false;
    }
    if (!paths::write(kRadioProfilePath, profile) || paths::read(kRadioProfilePath) != profile) {
        *error = "radio profile change was rejected by the kernel";
        return false;
    }
    if (profile == "lora")
        stop_gnss_startup();
    return true;
}

bool QuickSettingsService::set_lora_power(bool enabled, std::string *error)
{
    if (!lora_control_available()) {
        *error = "LR2021 radio controls are unavailable";
        return false;
    }
    if (!enabled)
        return paths::write(kLoraStatePath, "off");
    gnss_requested_ = false;
    if (!select_radio_profile("lora", error))
        return false;
    if (!paths::write(kLoraStatePath, "on") || paths::read(kLoraStatePath) != "on") {
        *error = "could not power on LR2021";
        return false;
    }
    return true;
}

bool QuickSettingsService::set_gnss_power(bool enabled, std::string *error)
{
    if (!lte_present()) {
        *error = "GPS is unavailable because this keyboard has no verified LTE module";
        return false;
    }
    if (!enabled) {
        gnss_requested_ = false;
        stop_gnss_startup();
        if (paths::read(kRadioProfilePath) == "nrf9151")
            return select_radio_profile("lora", error);
        return true;
    }
    if (!select_radio_profile("nrf9151", error))
        return false;
    stop_gnss_startup();
    const pid_t child = fork();
    if (child < 0) {
        *error = "could not start the nRF9151 GNSS activation worker";
        return false;
    }
    if (child == 0) {
        // The board-level profile switch takes care of the GPIO power rail;
        // the modem firmware itself needs longer before the AT proxy is ready.
        std::this_thread::sleep_for(std::chrono::seconds(8));
        const Nrf9151GnssResult result = set_nrf9151_gnss(true);
        _exit(result.gnss_active ? 0 : 1);
    }
    gnss_start_pid_ = child;
    gnss_requested_ = true;
    gps_state_ = "starting";
    return true;
}

bool QuickSettingsService::handle_request(const std::string &raw_request, State *state,
                                          std::string *error)
{
    const std::string request = trim(raw_request);
    if (request == "GET_STATE") {
        *state = collect_state();
        refresh(state);
        return true;
    }
    const std::string prefix = "SET ";
    if (request.rfind(prefix, 0) == 0) {
        const std::size_t space = request.find(' ', prefix.size());
        if (space == std::string::npos) {
            *error = "SET requires a key and a value";
            return false;
        }
        const std::string key = request.substr(prefix.size(), space - prefix.size());
        const std::string value = request.substr(space + 1);
        bool ok = false;
        if ((key == "display-brightness" || key == "keyboard-backlight") && parse_percent(value)) {
            ok = set_control(key == "display-brightness" ? "display-brightness" : "keyboard-brightness", value) == 0;
            if (!ok)
                *error = "backlight control rejected the requested value";
        } else if (key == "speaker-volume" && parse_percent(value)) {
            ok = paths::run_quietly(
                {"/usr/bin/amixer", "-c", "0", "sset", "PCM", value + "%"}) == 0;
            if (!ok)
                *error = "physical speaker volume control failed";
        } else if (key == "speaker-mute" &&
                   (value == "mute" || value == "unmute" || value == "toggle")) {
            ok = paths::run_quietly(
                {"/usr/bin/amixer", "-c", "0", "sset", "PCM", value}) == 0;
            if (!ok)
                *error = "physical speaker mute control failed";
        } else if (key == "speaker-route" && (value == "external" || value == "internal")) {
            ok = paths::run_quietly({"/usr/local/bin/tdvp-audio-route", value}) == 0;
            if (!ok)
                *error = "audio route change failed";
        } else if (key == "radio-profile") {
            ok = select_radio_profile(value, error);
        } else if (key == "lora-power" && (value == "on" || value == "off")) {
            ok = set_lora_power(value == "on", error);
        } else if (key == "gnss-power" && (value == "on" || value == "off")) {
            ok = set_gnss_power(value == "on", error);
        } else {
            *error = "unsupported quick-settings control";
            return false;
        }
        if (!ok && error->empty())
            *error = "hardware control failed";
        if (!ok)
            return false;
        *state = collect_state();
        refresh(state);
        return true;
    }
    if (request == "SYSTEM lock") {
        *error = "screen locking is an authenticated Wayland session action";
        return false;
    }
    if (request == "SYSTEM reboot" || request == "SYSTEM poweroff") {
        const char *verb = request == "SYSTEM reboot" ? "reboot" : "poweroff";
        const bool ok = paths::run_quietly({"/bin/systemctl", verb}) == 0;
        if (!ok)
            *error = "system manager rejected the power action";
        if (ok) {
            *state = collect_state();
            refresh(state);
        }
        return ok;
    }
    *error = "unsupported quick-settings request";
    return false;
}

void QuickSettingsService::process_requests()
{
    if (listener_ < 0)
        return;
    for (;;) {
        const int client = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            return;
        }
        ucred credentials {};
        socklen_t credentials_size = sizeof(credentials);
        const bool authorized = getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials,
                                           &credentials_size) == 0 &&
            credentials.uid == desktop_uid_;
        char request[kMaximumRequestBytes + 1] {};
        const ssize_t received = authorized ? recv(client, request, kMaximumRequestBytes, 0) : -1;
        State response_state;
        std::string error;
        const bool ok = authorized && received > 0 &&
            handle_request(std::string(request, static_cast<std::size_t>(received)), &response_state, &error);
        const std::string response = ok ? serialize_ok(response_state) :
            serialize_error(authorized ? (error.empty() ? "invalid request" : error) : "access denied");
        (void)send(client, response.data(), response.size(), MSG_NOSIGNAL);
        close(client);
    }
}

}  // namespace vpl::hardware

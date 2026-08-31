#pragma once

#include "contract.hpp"

#include <string>
#include <sys/types.h>

namespace vpl::hardware {

// Root-owned control boundary for the optional tdvp-quick-settings Wayland
// client.  It has no desktop-supervision role: Labwc, PCManFM and wf-panel-pi
// continue to run independently when this service socket has no clients.
class QuickSettingsService {
public:
    QuickSettingsService() = default;
    ~QuickSettingsService();

    QuickSettingsService(const QuickSettingsService &) = delete;
    QuickSettingsService &operator=(const QuickSettingsService &) = delete;

    [[nodiscard]] bool initialise();
    void refresh(State *state);
    void process_requests();

private:
    void update_lte_detection(const State &state);
    void stop_lte_probe();
    void update_gnss_startup();
    void stop_gnss_startup();
    [[nodiscard]] bool handle_request(const std::string &request, State *state,
                                      std::string *error);
    [[nodiscard]] bool select_radio_profile(const std::string &profile, std::string *error);
    [[nodiscard]] bool set_lora_power(bool enabled, std::string *error);
    [[nodiscard]] bool set_gnss_power(bool enabled, std::string *error);
    [[nodiscard]] bool lte_present() const;

    int listener_ = -1;
    uid_t desktop_uid_ = static_cast<uid_t>(-1);
    gid_t desktop_gid_ = static_cast<gid_t>(-1);
    pid_t lte_probe_pid_ = -1;
    pid_t gnss_start_pid_ = -1;
    bool gnss_requested_ = false;
    std::string lte_sku_state_ = "detached";
    std::string gps_state_ = "off";
    State last_state_;
};

}  // namespace vpl::hardware

#include "contract.hpp"

#include "paths.hpp"
#include "quick_settings_service.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>

namespace {

volatile std::sig_atomic_t keep_running = 1;

void stop(int)
{
    keep_running = 0;
}

std::string serialize(const vpl::hardware::State &state)
{
    std::ostringstream result;
    for (const auto &[key, value] : state)
        result << key << '=' << value << '\n';
    return result.str();
}

bool ensure_runtime_directory()
{
    if (mkdir("/run/vicliu-pocket-linux-hardware", 0755) == 0)
        return true;
    return errno == EEXIST;
}

}  // namespace

namespace vpl::hardware {

int run_daemon()
{
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    QuickSettingsService quick_settings;
    (void)quick_settings.initialise();

    while (keep_running) {
        if (ensure_runtime_directory()) {
            State state = collect_state();
            quick_settings.refresh(&state);
            (void)paths::write_atomically("/run/vicliu-pocket-linux-hardware/status.env", serialize(state));
        }
        // The status contract is refreshed at a deliberately low rate, but
        // the optional touch control center must not make a user wait two
        // seconds for a brightness or radio request.  Poll the local socket
        // in short idle slices without creating another resident process.
        for (int slice = 0; keep_running && slice < 20; ++slice) {
            quick_settings.process_requests();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}

}  // namespace vpl::hardware

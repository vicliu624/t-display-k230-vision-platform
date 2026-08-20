#include "contract.hpp"

#include "paths.hpp"

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

    while (keep_running) {
        if (ensure_runtime_directory()) {
            const State state = collect_state();
            (void)paths::write_atomically("/run/vicliu-pocket-linux-hardware/status.env", serialize(state));
        }
        for (int second = 0; keep_running && second < 2; ++second)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}

}  // namespace vpl::hardware

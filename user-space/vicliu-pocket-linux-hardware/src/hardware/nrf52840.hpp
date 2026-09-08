#pragma once

#include <chrono>
#include <csignal>
#include <map>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <vector>

namespace vpl::hardware {

struct Nrf52840Reply {
    std::vector<std::string> responses;
    std::vector<std::string> events;
};

// Single-owner, synchronous command boundary over an asynchronous wire protocol.
// Not HCI; no power/reset/DFU commands. Keep one instance for a complete BLE
// interaction. A timed-out instance cannot be reused or automatically recovered.
class Nrf52840Session {
public:
    explicit Nrf52840Session(int timeout_ms = 2500,
                            const volatile sig_atomic_t *cancelled = nullptr);
    ~Nrf52840Session();
    Nrf52840Session(const Nrf52840Session &) = delete;
    Nrf52840Session &operator=(const Nrf52840Session &) = delete;
    Nrf52840Reply open(const std::string &device);
    Nrf52840Reply execute(const std::string &request);
    void close();

private:
    using Clock = std::chrono::steady_clock;
    enum class Operation { Version, Status, Scan, Connect, Disconnect,
                           Services, Characteristics, Descriptors, Read, Write };
    Nrf52840Reply transact(const std::string &wire, Operation op,
                          unsigned argument = 0, unsigned detail = 0,
                          int extra_ms = 0);
    void send(const std::string &wire, Clock::time_point deadline);
    std::string line(Clock::time_point deadline);
    bool event(const std::string &line, Nrf52840Reply *reply);
    void check(Clock::time_point deadline);
    [[noreturn]] void fail(const std::string &message);

    int fd_ = -1;
    int timeout_ms_;
    const volatile sig_atomic_t *cancelled_;
    termios original_ {};
    bool restore_ = false;
    bool exclusive_ = false;
    bool identified_ = false;
    bool opened_ = false;
    bool poisoned_ = false;
    bool inherited_connection_ = false;
    bool remote_busy_ = false;
    int connection_ = -1;
    std::string pending_;
    std::map<unsigned, std::string> scanned_;
};

} // namespace vpl::hardware

#include "nrf9151.hpp"

#include "paths.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

namespace vpl::hardware {
namespace {

constexpr char kProfilePath[] = "/sys/devices/platform/radio-mux/profile";
constexpr char kUartDevice[] = "/dev/ttyS2";
// Keep the first request aligned with the vendor nRF9151 example. It is a
// read-only identity query and gives us an equivalent UART-path check.
constexpr std::array<const char *, 4> kProbeCommands {
    "AT%XICCID\r\n",
    "AT\r\n",
    "ATI\r\n",
    "AT+CGSN\r\n",
};
constexpr std::chrono::milliseconds kStartupDelay {8000};
constexpr std::chrono::milliseconds kResponseTimeout {1500};

class ProfileRestoreGuard {
public:
    explicit ProfileRestoreGuard(std::string profile)
        : profile_(std::move(profile))
    {
    }

    ~ProfileRestoreGuard()
    {
        if (!profile_.empty())
            (void)paths::write(kProfilePath, profile_);
    }

    bool restore()
    {
        return profile_.empty() || paths::write(kProfilePath, profile_);
    }

private:
    std::string profile_;
};

bool configure_uart(int descriptor, std::string *error)
{
    termios options {};
    if (tcgetattr(descriptor, &options) != 0) {
        if (error)
            *error = std::string("tcgetattr ") + kUartDevice + ": " + std::strerror(errno);
        return false;
    }

    options.c_iflag = 0;
    options.c_oflag = 0;
    options.c_lflag = 0;
    options.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB | CSTOPB | CRTSCTS));
    options.c_cflag |= CS8 | CLOCAL | CREAD;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    if (cfsetispeed(&options, B115200) != 0 || cfsetospeed(&options, B115200) != 0 ||
        tcsetattr(descriptor, TCSANOW, &options) != 0) {
        if (error)
            *error = std::string("configure ") + kUartDevice + ": " + std::strerror(errno);
        return false;
    }

    int modem_bits = TIOCM_DTR | TIOCM_RTS;
    if (ioctl(descriptor, TIOCMBIS, &modem_bits) != 0 && errno != ENOTTY) {
        if (error)
            *error = std::string("assert UART modem-control lines: ") + std::strerror(errno);
        return false;
    }
    return tcflush(descriptor, TCIOFLUSH) == 0;
}

bool write_all(int descriptor, const char *data, std::size_t size, std::string *error)
{
    while (size > 0) {
        const ssize_t written = write(descriptor, data, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (error)
                *error = std::string("write ") + kUartDevice + ": " + std::strerror(errno);
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool read_response(int descriptor, std::string *response, std::string *error)
{
    const auto deadline = std::chrono::steady_clock::now() + kResponseTimeout;
    std::array<char, 128> buffer {};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd poll_fd {descriptor, POLLIN, 0};
        const int poll_result = poll(&poll_fd, 1, static_cast<int>(remaining.count()));
        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            if (error)
                *error = std::string("poll ") + kUartDevice + ": " + std::strerror(errno);
            return false;
        }
        if (poll_result == 0)
            break;
        if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (error)
                *error = "UART2 returned a hangup or error while probing nRF9151";
            return false;
        }
        if ((poll_fd.revents & POLLIN) == 0)
            continue;

        const ssize_t received = read(descriptor, buffer.data(), buffer.size());
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            if (error)
                *error = std::string("read ") + kUartDevice + ": " + std::strerror(errno);
            return false;
        }
        if (received > 0)
            response->append(buffer.data(), static_cast<std::size_t>(received));
        if (response->find("OK") != std::string::npos)
            return true;
    }
    return true;
}

}  // namespace

Nrf9151ProbeResult probe_nrf9151()
{
    Nrf9151ProbeResult result;
    result.profile_available = paths::exists(kProfilePath);
    if (!result.profile_available) {
        result.error = std::string("missing radio profile selector: ") + kProfilePath;
        return result;
    }

    result.original_profile = paths::read(kProfilePath);
    ProfileRestoreGuard restore_guard(result.original_profile);
    if (!paths::write(kProfilePath, "nrf9151") || paths::read(kProfilePath) != "nrf9151") {
        result.error = "could not select the nRF9151 radio profile";
        result.restored_profile = restore_guard.restore();
        return result;
    }
    result.profile_selected = true;
    std::this_thread::sleep_for(kStartupDelay);

    const int descriptor = open(kUartDevice, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        result.error = std::string("open ") + kUartDevice + ": " + std::strerror(errno);
        result.restored_profile = restore_guard.restore();
        return result;
    }
    result.uart_opened = true;

    std::string error;
    if (configure_uart(descriptor, &error)) {
        result.uart_configured = true;
        for (const char *command : kProbeCommands) {
            if (!write_all(descriptor, command, std::strlen(command), &error))
                break;
            if (!read_response(descriptor, &result.response, &error))
                break;
            if (result.response.find("OK") != std::string::npos)
                break;
        }
    }
    close(descriptor);

    result.restored_profile = restore_guard.restore();
    if (!error.empty())
        result.error = std::move(error);
    if (result.response.empty() && result.error.empty())
        result.error = "nRF9151 returned no bytes after an 8 second startup wait and four identity/AT queries";
    return result;
}

}  // namespace vpl::hardware

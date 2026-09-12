#include "nrf52840.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace vpl::hardware {
namespace {
constexpr std::size_t kMaxLine = 2048;
constexpr std::size_t kMaxReply = 65536;

bool starts(const std::string &s, const std::string &prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> split(const std::string &s, char separator)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto end = s.find(separator, start);
        result.push_back(s.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return result;
        start = end + 1;
    }
}

unsigned number(const std::string &s, unsigned maximum = 65535, unsigned base = 10)
{
    if (s.empty()) throw std::invalid_argument("empty integer");
    unsigned value = 0;
    for (const unsigned char c : s) {
        const unsigned digit = c >= '0' && c <= '9' ? c - '0' :
            (c >= 'a' && c <= 'f' ? c - 'a' + 10 :
             (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99));
        if (digit >= base || digit > maximum || value > (maximum - digit) / base)
            throw std::invalid_argument("invalid or out-of-range integer");
        value = value * base + digit;
    }
    return value;
}

bool hex_bytes(const std::string &s, unsigned bytes)
{
    return s.size() == bytes * 2 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool address(const std::string &s)
{
    if (s.size() != 17) return false;
    for (unsigned i = 0; i < s.size(); ++i) {
        if (i % 3 == 2 ? s[i] != ':' : !std::isxdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    return true;
}

bool uuid(const std::string &s)
{
    const auto fields = split(s, ':');
    if (fields.size() != 2 || !number(fields[1], 255)) return false;
    return (fields[0].size() == 6 && starts(fields[0], "0x") && hex_bytes(fields[0].substr(2), 2)) ||
           hex_bytes(fields[0], 16);
}

std::string uppercase(std::string s)
{
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

Nrf52840Session::Nrf52840Session(int timeout_ms, const volatile sig_atomic_t *cancelled)
    : timeout_ms_(timeout_ms), cancelled_(cancelled)
{
    if (timeout_ms < 100 || timeout_ms > 45000)
        throw std::invalid_argument("timeout must be 100..45000 ms");
}

Nrf52840Session::~Nrf52840Session()
{
    try { close(); } catch (...) { /* Explicit close reports restoration errors. */ }
}

void Nrf52840Session::close()
{
    if (fd_ < 0) return;
    bool ok = true;
    if (restore_ && tcsetattr(fd_, TCSANOW, &original_) != 0) ok = false;
    if (exclusive_ && ioctl(fd_, TIOCNXCL) != 0) ok = false;
    if (::close(fd_) != 0) ok = false;
    fd_ = -1;
    identified_ = false;
    restore_ = exclusive_ = false;
    if (!ok) throw std::runtime_error("UART close/termios restoration failed");
}

[[noreturn]] void Nrf52840Session::fail(const std::string &message)
{
    poisoned_ = true;
    throw std::runtime_error(message);
}

void Nrf52840Session::check(Clock::time_point deadline)
{
    if (cancelled_ && *cancelled_) fail("cancelled; UART outcome may be uncertain");
    if (Clock::now() >= deadline) fail("timeout; session is unusable, no automatic retry");
}

Nrf52840Reply Nrf52840Session::open(const std::string &device)
{
    if (opened_ || poisoned_) throw std::runtime_error("session cannot be reopened");
    opened_ = true;
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) fail("open UART: " + std::string(std::strerror(errno)));
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) fail("UART owner already holds flock");
    int previously_exclusive = 0;
    if (ioctl(fd_, TIOCGEXCL, &previously_exclusive) != 0 || previously_exclusive)
        fail("UART already exclusive or exclusivity cannot be checked");
    if (ioctl(fd_, TIOCEXCL) != 0) fail("cannot make UART exclusive");
    exclusive_ = true;
    if (tcgetattr(fd_, &original_) != 0) fail("cannot snapshot UART termios");
    restore_ = true;
    termios options = original_;
    cfmakeraw(&options);
    options.c_cflag &= static_cast<tcflag_t>(~(CSIZE | PARENB | CSTOPB | CRTSCTS));
    options.c_cflag |= CS8 | CLOCAL | CREAD;
    options.c_cc[VMIN] = options.c_cc[VTIME] = 0;
    if (cfsetispeed(&options, B115200) || cfsetospeed(&options, B115200) ||
        tcsetattr(fd_, TCSANOW, &options) || tcflush(fd_, TCIOFLUSH))
        fail("cannot configure 115200 8N1 UART");
    auto identity = transact("AT+VER?", Operation::Version);
    identified_ = true;
    auto state = transact("AT+STATUS?", Operation::Status);
    inherited_connection_ = connection_ >= 0;
    identity.responses.insert(identity.responses.end(), state.responses.begin(), state.responses.end());
    identity.events.insert(identity.events.end(), state.events.begin(), state.events.end());
    return identity;
}

void Nrf52840Session::send(const std::string &wire, Clock::time_point deadline)
{
    const std::string bytes = wire + "\r\n";
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        check(deadline);
        const auto n = ::write(fd_, bytes.data() + sent, bytes.size() - sent);
        if (n > 0) { sent += static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno != EAGAIN && errno != EINTR) fail("UART write failed");
        pollfd p {fd_, POLLOUT, 0};
        const int rc = poll(&p, 1, 20);
        if ((rc < 0 && errno != EINTR) || (p.revents & (POLLHUP | POLLERR | POLLNVAL)))
            fail("UART write poll failed");
    }
    // No blocking tcdrain: a reply is required within the same absolute deadline.
}

std::string Nrf52840Session::line(Clock::time_point deadline)
{
    for (;;) {
        check(deadline);
        const auto newline = pending_.find('\n');
        if (newline != std::string::npos) {
            auto result = pending_.substr(0, newline);
            pending_.erase(0, newline + 1);
            if (!result.empty() && result.back() == '\r') result.pop_back();
            if (result.empty()) continue;
            if (result.size() > kMaxLine || std::any_of(result.begin(), result.end(), [](unsigned char c) {
                    return c < 32 || c == 127;
                })) fail("invalid UART line");
            return result;
        }
        if (pending_.size() > kMaxLine) fail("UART line exceeds limit");
        pollfd p {fd_, POLLIN, 0};
        const int rc = poll(&p, 1, 20);
        if (rc < 0) { if (errno == EINTR) continue; fail("UART read poll failed"); }
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) fail("UART hangup/error");
        if (!(p.revents & POLLIN)) continue;
        char bytes[256];
        const auto n = ::read(fd_, bytes, sizeof(bytes));
        if (n > 0) pending_.append(bytes, static_cast<std::size_t>(n));
        else if (n < 0 && errno != EAGAIN && errno != EINTR) fail("UART read failed");
    }
}

bool Nrf52840Session::event(const std::string &s, Nrf52840Reply *reply)
{
    if (starts(s, "+BOOT:") || s == "+GATT:TIMEOUT") fail("nRF reset or GATT timeout; session invalidated");
    if (starts(s, "+DISCONNECTED:")) {
        const auto fields = split(s.substr(14), ',');
        if (fields.size() != 2 || static_cast<int>(number(fields[0])) != connection_)
            fail("unexpected disconnected handle");
        connection_ = -1;
        reply->events.push_back(s);
        return true;
    }
    if (starts(s, "+NOTIFY:") || starts(s, "+IND:")) {
        const auto fields = split(s.substr(s.find(':') + 1), ',');
        if (connection_ < 0 || fields.size() != 3 || !number(fields[0]) ||
            !hex_bytes(fields[2], number(fields[1], 512))) fail("malformed notification");
        reply->events.push_back(s);
        return true;
    }
    if (starts(s, "+MESH:") || starts(s, "+MTU:") || starts(s, "+PHY:")) {
        reply->events.push_back(s);
        return true;
    }
    return false;
}

Nrf52840Reply Nrf52840Session::transact(const std::string &wire, Operation op,
                                     unsigned argument, unsigned detail, int extra_ms)
{
    if (fd_ < 0 || poisoned_) throw std::runtime_error("UART session is not usable");
    Nrf52840Reply reply;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_ + extra_ms);
    try {
        // A preceding request may leave unsolicited events in the same read.
        // Never let an orphan response/OK satisfy the next transaction.
        pollfd p {fd_, POLLIN, 0};
        unsigned idle_lines = 0;
        while (!pending_.empty() || (poll(&p, 1, 0) > 0 && (p.revents & POLLIN))) {
            if (++idle_lines > 256) fail("unsolicited event backlog exceeds limit");
            if (!event(line(deadline), &reply)) fail("orphan UART result before request");
        }
        if (op != Operation::Version && op != Operation::Status &&
            op != Operation::Scan && op != Operation::Connect && connection_ < 0)
            throw std::runtime_error("BLE connection is no longer present");
        send(wire, deadline);
        bool ack = false, result = false;
        std::size_t total = 0;
        unsigned count = 0;
        while (!(ack && result)) {
            const auto s = line(deadline);
            total += s.size();
            if (total > kMaxReply || ++count > 512) fail("UART reply exceeds limit");
            if (s == "OK") {
                if (ack) fail("duplicate OK");
                ack = true;
                continue;
            }
            if (starts(s, "ERR:")) fail("nRF rejected request: " + s);
            if (starts(s, "+DISCONNECTED:") && op == Operation::Disconnect) {
                if (!event(s, &reply)) fail("invalid disconnect event");
                result = true;
                continue;
            }
            if (event(s, &reply)) {
                if (connection_ < 0 && op != Operation::Version && op != Operation::Status &&
                    op != Operation::Scan && op != Operation::Connect)
                    fail("BLE disconnected during operation");
                continue;
            }
            if (result) fail("duplicate or trailing result before OK");
            bool matched = false;
            if (op == Operation::Version && starts(s, "+VER:K230_NRF52840_AT,")) {
                const auto version = s.substr(std::strlen("+VER:K230_NRF52840_AT,"));
                if (version.empty() || version.size() > 64 ||
                    !std::all_of(version.begin(), version.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+';
                    })) fail("invalid nRF firmware identity");
                matched = result = true;
            } else if (op == Operation::Status && starts(s, "+STATUS:")) {
                const auto fields = split(s.substr(8), ',');
                unsigned first = 1;
                if (fields[0] == "CONNECTED") {
                    if (fields.size() < 2) fail("missing connection handle");
                    if (connection_ < 0) inherited_connection_ = true;
                    connection_ = static_cast<int>(number(fields[1], 65534));
                    first = 2;
                } else if (fields[0] == "READY" || fields[0] == "SCANNING") connection_ = -1;
                else fail("invalid nRF state");
                const std::vector<std::string> keys {"COUNT", "GATT", "SVC", "CHR", "DESC", "MESH_ADV", "MESH_CONN", "MESH_QUEUE"};
                if (fields.size() != first + keys.size()) fail("incomplete nRF status");
                remote_busy_ = fields[0] == "SCANNING";
                for (unsigned i = 0; i < keys.size(); ++i) {
                    const auto pair = split(fields[first + i], '=');
                    if (pair.size() != 2 || pair[0] != keys[i]) fail("invalid nRF status field");
                    const auto value = number(pair[1], i == 1 || i == 5 || i == 6 ? 1 : 65535);
                    if (i == 1 && value) remote_busy_ = true;
                }
                matched = result = true;
            } else if (op == Operation::Scan && starts(s, "+SCAN:")) {
                const auto fields = split(s.substr(6), ',');
                if (fields[0] == "DONE") {
                    if (fields.size() != 2 || number(fields[1], 255) != scanned_.size())
                        fail("scan count does not match received devices");
                    result = true;
                } else {
                    if (fields.size() < 5 || !address(fields[1])) fail("invalid scan record");
                    const auto index = number(fields[0], 254);
                    const auto rssi = fields[2];
                    (void)number(starts(rssi, "-") ? rssi.substr(1) : rssi, starts(rssi, "-") ? 128 : 127);
                    (void)number(fields[3], 1);
                    const std::string mac = uppercase(fields[1]);
                    if (scanned_.count(index) && scanned_[index] != mac) fail("scan index changed identity");
                    scanned_[index] = mac;
                }
                matched = true;
            } else if (op == Operation::Connect && starts(s, "+CONNECTED:")) {
                connection_ = static_cast<int>(number(s.substr(11), 65534));
                matched = result = true;
            } else if ((op == Operation::Services && starts(s, "+GATTS:")) ||
                       (op == Operation::Characteristics && starts(s, "+GATTC:")) ||
                       (op == Operation::Descriptors && starts(s, "+GATTD:"))) {
                const auto fields = split(s.substr(7), ',');
                if (fields[0] == "DONE") {
                    const bool services = op == Operation::Services;
                    if (fields.size() != (services ? 3U : 4U)) fail("invalid discovery completion");
                    if (!services && number(fields[1]) != argument) fail("wrong discovery index");
                    (void)number(fields[fields.size() - 2], 255);
                    const auto status = number(fields.back(), 65535, 16);
                    // ATT attribute-not-found (0x10a) is the normal discovery terminator.
                    if (status != 0 && status != 0x10a) fail("GATT discovery failed: " + s);
                    result = true;
                } else if (fields.size() != (op == Operation::Characteristics ? 8U : 4U)) {
                    fail("invalid discovery record");
                } else {
                    (void)number(fields[0], 254);
                    const unsigned uuid_field = op == Operation::Services ? 1 : 2;
                    if (!uuid(fields[uuid_field])) fail("invalid discovery UUID");
                    if (op == Operation::Services) {
                        if (!number(fields[2]) || number(fields[3]) < number(fields[2]))
                            fail("invalid service handle range");
                    } else {
                        if (number(fields[1], 254) != argument) fail("discovery record belongs to wrong index");
                        if (op == Operation::Descriptors) {
                            if (!number(fields[3])) fail("invalid descriptor handle");
                        } else {
                            const auto declaration = number(fields[3]);
                            const auto value = number(fields[4]);
                            const auto end = number(fields[5]);
                            (void)number(fields[6], 255);
                            const auto cccd = number(fields[7]);
                            if (!declaration || value <= declaration || end < value ||
                                (cccd && (cccd <= value || cccd > end)))
                                fail("invalid characteristic handle range");
                        }
                    }
                }
                matched = true;
            } else if (op == Operation::Read && starts(s, "+READ:")) {
                const auto fields = split(s.substr(6), ',');
                if (fields.size() != 5 || number(fields[0]) != argument || number(fields[1]) != detail ||
                    !hex_bytes(fields[3], number(fields[2], 512))) fail("invalid GATT read result");
                if (number(fields[4], 65535, 16) != 0) fail("GATT read failed: " + s);
                matched = result = true;
            } else if (op == Operation::Write && starts(s, "+WRITE:")) {
                const auto fields = split(s.substr(7), ',');
                if (fields.size() != 3 || number(fields[0]) != argument || number(fields[1]) != detail)
                    fail("invalid GATT write result");
                if (number(fields[2], 65535, 16) != 0) fail("GATT write failed: " + s);
                matched = result = true;
            }
            if (!matched) fail("unexpected UART result: " + s);
            reply.responses.push_back(s);
        }
        return reply;
    } catch (...) {
        poisoned_ = true;
        throw;
    }
}

Nrf52840Reply Nrf52840Session::execute(const std::string &request)
{
    if (!identified_ || poisoned_) throw std::runtime_error("nRF identity/session not ready");
    if (request.size() > 1024 || request.find_first_of("\r\n\t") != std::string::npos)
        throw std::invalid_argument("invalid command framing");
    const auto args = split(request, ' ');
    if (args == std::vector<std::string>{"status"}) return transact("AT+STATUS?", Operation::Status);
    if (args.size() == 2 && args[0] == "listen") {
        const auto seconds = number(args[1], 30);
        if (!seconds) throw std::invalid_argument("listen requires 1..30 seconds");
        Nrf52840Reply reply;
        const auto deadline = Clock::now() + std::chrono::seconds(seconds);
        unsigned count = 0;
        try {
            while (Clock::now() < deadline) {
                if (cancelled_ && *cancelled_) fail("listen cancelled");
                pollfd p {fd_, POLLIN, 0};
                const int rc = poll(&p, 1, pending_.empty() ? 20 : 0);
                if ((rc < 0 && errno != EINTR) || (p.revents & (POLLHUP | POLLERR | POLLNVAL)))
                    fail("listen UART hangup/error");
                if (pending_.empty() && !(p.revents & POLLIN)) continue;
                if (++count > 256) fail("listen event limit reached");
                if (!event(line(deadline), &reply)) fail("unexpected result while listening");
            }
        } catch (...) { poisoned_ = true; throw; }
        return reply;
    }
    if (remote_busy_ || inherited_connection_)
        throw std::runtime_error("nRF has pre-existing activity; this session will not take it over");
    if (args.size() == 2 && args[0] == "scan") {
        const auto seconds = number(args[1], 30);
        if (!seconds || connection_ >= 0) throw std::invalid_argument("scan requires 1..30 seconds and no connection");
        scanned_.clear();
        return transact("AT+SCAN=" + std::to_string(seconds), Operation::Scan, 0, 0, seconds * 1000);
    }
    if (args.size() == 2 && args[0] == "connect" && address(args[1])) {
        if (connection_ >= 0) throw std::invalid_argument("already connected");
        // Firmware treats a digit-leading address as an index. Resolve the exact
        // user-selected address in this session's scan cache and send its index.
        const auto mac = uppercase(args[1]);
        unsigned matches = 0, index = 0;
        for (const auto &entry : scanned_) if (entry.second == mac) { ++matches; index = entry.first; }
        if (matches != 1) throw std::invalid_argument("address not uniquely present in this session's completed scan");
        return transact("AT+CONN=" + std::to_string(index), Operation::Connect);
    }
    if (connection_ < 0) throw std::invalid_argument("command requires a connection owned by this session");
    if (args == std::vector<std::string>{"disconnect"}) return transact("AT+DISC", Operation::Disconnect);
    if (args == std::vector<std::string>{"services"}) return transact("AT+GATTS?", Operation::Services);
    if (args.size() == 2 && (args[0] == "characteristics" || args[0] == "descriptors")) {
        const auto index = number(args[1], 254);
        return transact((args[0] == "characteristics" ? "AT+GATTC=" : "AT+GATTD=") + std::to_string(index),
            args[0] == "characteristics" ? Operation::Characteristics : Operation::Descriptors, index);
    }
    if ((args.size() == 2 || args.size() == 3) && args[0] == "read") {
        const auto handle = number(args[1]);
        const auto offset = args.size() == 3 ? number(args[2]) : 0;
        if (!handle) throw std::invalid_argument("invalid attribute handle");
        return transact("AT+READ=" + std::to_string(handle) + "," + std::to_string(offset), Operation::Read, handle, offset);
    }
    if (args.size() == 3 && (args[0] == "write" || args[0] == "cccd")) {
        const auto handle = number(args[1]);
        std::string bytes = args[2];
        if (args[0] == "cccd") {
            if (bytes == "off") bytes = "0000";
            else if (bytes == "notify") bytes = "0100";
            else if (bytes == "indicate") bytes = "0200";
            else throw std::invalid_argument("cccd mode must be off/notify/indicate");
        }
        if (!handle || bytes.empty() || bytes.size() > 488 || !hex_bytes(bytes, bytes.size() / 2))
            throw std::invalid_argument("write requires handle and 1..244 hexadecimal bytes");
        return transact("AT+WRITE=" + std::to_string(handle) + "," + uppercase(bytes) + ",REQ",
                        Operation::Write, handle, bytes.size() / 2);
    }
    throw std::invalid_argument("unsupported command (no reset/DFU/power/raw AT interface)");
}

} // namespace vpl::hardware

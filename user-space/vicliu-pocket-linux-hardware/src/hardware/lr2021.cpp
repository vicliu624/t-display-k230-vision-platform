#include "lr2021.hpp"

#include "paths.hpp"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
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
constexpr char kLoraStatePath[] = "/sys/devices/platform/radio-mux/lora_state";
constexpr char kSpiDevice[] = "/dev/spidev0.0";
constexpr uint32_t kSpiSpeedHz = 5000000;
constexpr std::array<uint8_t, 2> kGetVersionCommand {{0x01, 0x01}};
constexpr std::array<uint8_t, 2> kNopRead {{0x00, 0x00}};

class RadioRestoreGuard {
public:
    explicit RadioRestoreGuard(std::string profile)
        : profile_(std::move(profile))
    {
    }

    ~RadioRestoreGuard()
    {
        std::string ignored;
        (void)power_off(&ignored);
        if (!profile_.empty())
            (void)paths::write(kProfilePath, profile_);
    }

    bool power_on(std::string *error)
    {
        if (!paths::write(kLoraStatePath, "on")) {
            if (error)
                *error = std::string("write ") + kLoraStatePath + "=on failed";
            return false;
        }
        powered_ = paths::read(kLoraStatePath) == "on";
        if (!powered_ && error)
            *error = std::string("LoRa power state did not become on at ") + kLoraStatePath;
        return powered_;
    }

    bool power_off(std::string *error)
    {
        if (!powered_)
            return true;
        if (!paths::write(kLoraStatePath, "off")) {
            if (error)
                *error = std::string("write ") + kLoraStatePath + "=off failed";
            return false;
        }
        powered_ = false;
        return true;
    }

private:
    std::string profile_;
    bool powered_ = false;
};

bool set_spi_option(int descriptor, unsigned long request, const void *value, std::string *error)
{
    if (ioctl(descriptor, request, value) == 0)
        return true;
    if (error)
        *error = std::string("configure ") + kSpiDevice + ": " + std::strerror(errno);
    return false;
}

bool transfer(int descriptor, const uint8_t *transmit, uint8_t *receive, std::size_t size,
              std::string *error)
{
    spi_ioc_transfer transfer {};
    transfer.tx_buf = reinterpret_cast<__u64>(transmit);
    transfer.rx_buf = reinterpret_cast<__u64>(receive);
    transfer.len = static_cast<__u32>(size);
    transfer.speed_hz = kSpiSpeedHz;
    transfer.bits_per_word = 8;
    if (ioctl(descriptor, SPI_IOC_MESSAGE(1), &transfer) == static_cast<int>(size))
        return true;
    if (error)
        *error = std::string("LR2021 SPI transfer: ") + std::strerror(errno);
    return false;
}

bool read_version(uint8_t *major, uint8_t *minor, std::string *error)
{
    const int descriptor = open(kSpiDevice, O_RDWR | O_CLOEXEC);
    if (descriptor < 0) {
        if (error)
            *error = std::string("open ") + kSpiDevice + ": " + std::strerror(errno);
        return false;
    }

    const uint8_t mode = SPI_MODE_0;
    const uint8_t bits_per_word = 8;
    bool ok = set_spi_option(descriptor, SPI_IOC_WR_MODE, &mode, error) &&
              set_spi_option(descriptor, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word, error) &&
              set_spi_option(descriptor, SPI_IOC_WR_MAX_SPEED_HZ, &kSpiSpeedHz, error);
    std::array<uint8_t, 2> ignored {};
    std::array<uint8_t, 2> response {};
    if (ok)
        ok = transfer(descriptor, kGetVersionCommand.data(), ignored.data(), kGetVersionCommand.size(), error);
    if (ok)
        ok = transfer(descriptor, kNopRead.data(), response.data(), kNopRead.size(), error);
    close(descriptor);
    if (!ok)
        return false;

    *major = response[0];
    *minor = response[1];
    return true;
}

}  // namespace

Lr2021ProbeResult probe_lr2021()
{
    Lr2021ProbeResult result;
    if (!paths::exists(kProfilePath) || !paths::exists(kLoraStatePath)) {
        result.error = std::string("missing LR2021 radio controls: ") + kProfilePath + " and " +
                       kLoraStatePath;
        return result;
    }

    const std::string original_profile = paths::read(kProfilePath);
    RadioRestoreGuard restore_guard(original_profile);
    if (!paths::write(kProfilePath, "lora") || paths::read(kProfilePath) != "lora") {
        result.error = "could not select the LR2021 radio profile";
        return result;
    }
    result.profile_selected = true;

    std::string error;
    if (!restore_guard.power_on(&error)) {
        result.error = std::move(error);
        return result;
    }
    result.power_enabled = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    if (!read_version(&result.firmware_major, &result.firmware_minor, &error)) {
        result.error = std::move(error);
    } else {
        result.command_sent = true;
        result.response_valid = !((result.firmware_major == 0x00 && result.firmware_minor == 0x00) ||
                                  (result.firmware_major == 0xff && result.firmware_minor == 0xff));
        if (!result.response_valid)
            result.error = "LR2021 GET_VERSION returned an electrically invalid all-zero or all-one version";
    }

    std::string power_off_error;
    result.power_disabled = restore_guard.power_off(&power_off_error);
    if (!result.power_disabled && result.error.empty())
        result.error = std::move(power_off_error);
    return result;
}

}  // namespace vpl::hardware

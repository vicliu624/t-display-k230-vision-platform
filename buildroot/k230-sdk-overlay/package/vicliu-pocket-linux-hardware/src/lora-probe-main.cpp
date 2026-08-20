#include "hardware/lr2021.hpp"

#include <iomanip>
#include <iostream>

int main()
{
    const vpl::hardware::Lr2021ProbeResult result = vpl::hardware::probe_lr2021();
    std::cout << "radio=lr2021\n";
    std::cout << "spi=/dev/spidev0.0\n";
    std::cout << "profile_selected=" << (result.profile_selected ? 1 : 0) << '\n';
    std::cout << "power_enabled=" << (result.power_enabled ? 1 : 0) << '\n';
    std::cout << "power_disabled=" << (result.power_disabled ? 1 : 0) << '\n';
    std::cout << "command_sent=" << (result.command_sent ? 1 : 0) << '\n';
    std::cout << "firmware_major=" << std::dec << static_cast<unsigned int>(result.firmware_major) << '\n';
    std::cout << "firmware_minor=" << static_cast<unsigned int>(result.firmware_minor) << '\n';
    std::cout << "response_valid=" << (result.response_valid ? 1 : 0) << '\n';
    if (!result.error.empty())
        std::cout << "error=" << result.error << '\n';
    return result.response_valid ? 0 : 1;
}

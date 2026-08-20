#include "hardware/nrf9151.hpp"

#include <iostream>

namespace {

std::string escape(const std::string &value)
{
    std::string escaped;
    for (const char character : value) {
        switch (character) {
        case '\r':
            escaped += "\\r";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

}  // namespace

int main()
{
    const vpl::hardware::Nrf9151ProbeResult result = vpl::hardware::probe_nrf9151();
    std::cout << "radio=nrf9151\n";
    std::cout << "profile=/sys/devices/platform/radio-mux/profile\n";
    std::cout << "uart=/dev/ttyS2\n";
    std::cout << "profile_available=" << (result.profile_available ? 1 : 0) << '\n';
    std::cout << "profile_selected=" << (result.profile_selected ? 1 : 0) << '\n';
    std::cout << "uart_opened=" << (result.uart_opened ? 1 : 0) << '\n';
    std::cout << "uart_configured=" << (result.uart_configured ? 1 : 0) << '\n';
    std::cout << "original_profile=" << result.original_profile << '\n';
    std::cout << "response=" << escape(result.response) << '\n';
    std::cout << "response_valid=" << (result.response.find("OK") != std::string::npos ? 1 : 0) << '\n';
    std::cout << "restored_profile=" << (result.restored_profile ? 1 : 0) << '\n';
    if (!result.error.empty())
        std::cout << "error=" << result.error << '\n';
    return result.response.find("OK") != std::string::npos ? 0 : 1;
}

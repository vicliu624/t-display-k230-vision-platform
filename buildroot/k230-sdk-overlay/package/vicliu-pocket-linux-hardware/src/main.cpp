#include "hardware/contract.hpp"

#include <iostream>
#include <string>

namespace {

int usage()
{
    std::cerr << "Usage: vpl-hwctl status | initialise | daemon | get <control> | set <control> <0-100>\n";
    return 64;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3 && argc != 4)
        return usage();
    const std::string command(argv[1]);
    if (command == "status" && argc == 2) {
        for (const auto &[key, value] : vpl::hardware::collect_state())
            std::cout << key << '=' << value << '\n';
        return 0;
    }
    if (command == "initialise" && argc == 2)
        return vpl::hardware::initialise();
    if (command == "daemon" && argc == 2)
        return vpl::hardware::run_daemon();
    if (command == "get" && argc == 3) {
        std::string value;
        const int result = vpl::hardware::get_control(argv[2], &value);
        if (result == 0)
            std::cout << value << '\n';
        return result;
    }
    if (command == "set" && argc == 4)
        return vpl::hardware::set_control(argv[2], argv[3]);
    return usage();
}

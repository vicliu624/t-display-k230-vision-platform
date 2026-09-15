#include "hardware/nrf52840.hpp"

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
volatile sig_atomic_t cancelled = 0;
void cancel(int) { cancelled = 1; }

std::string quoted(const std::string &text)
{
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32 || c >= 127) {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            result += escaped;
        } else result += static_cast<char>(c);
    }
    return result + '"';
}

void output(const vpl::hardware::Nrf52840Reply &reply, const std::string &command)
{
    for (const auto &s : reply.events) std::cout << "{\"event\":" << quoted(s) << "}\n";
    for (const auto &s : reply.responses) std::cout << "{\"response\":" << quoted(s) << "}\n";
    std::cout << "{\"completed\":" << quoted(command) << "}\n" << std::flush;
}
}

int main(int argc, char **argv)
{
    std::unique_ptr<vpl::hardware::Nrf52840Session> session;
    try {
        std::string device = "/dev/ttyS1";
        int timeout = 2500;
        std::vector<std::string> commands;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "tdvp-nrf52840 [--device UART] [--timeout-ms 100..45000] [COMMAND ...]\n"
                    "Each COMMAND is one quoted argument. Default: identity/status only.\n"
                    "Commands: status; scan SECONDS; connect MAC; disconnect; services;\n"
                    "characteristics INDEX; descriptors INDEX; read HANDLE [OFFSET];\n"
                    "write HANDLE HEX; cccd HANDLE off|notify|indicate; listen SECONDS.\n"
                    "Keeps one exclusive session. Commands may transmit BLE or write a peer.\n"
                    "No auto-disconnect/reset/DFU/retry. An unfinished connection can survive exit.\n";
                return 0;
            }
            if (arg == "--device" && i + 1 < argc) device = argv[++i];
            else if (arg == "--timeout-ms" && i + 1 < argc) {
                const std::string value = argv[++i];
                std::size_t consumed = 0;
                timeout = std::stoi(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("invalid timeout");
            } else if (arg.rfind("--", 0) == 0) throw std::invalid_argument("invalid option");
            else commands.push_back(arg);
        }
        struct sigaction action {};
        action.sa_handler = cancel;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
            throw std::runtime_error("cannot install cancellation handler");
        session = std::make_unique<vpl::hardware::Nrf52840Session>(timeout, &cancelled);
        output(session->open(device), "identity/status");
        for (const auto &command : commands) output(session->execute(command), command);
        session->close();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "{\"error\":" << quoted(e.what()) << "}\n";
        if (session) {
            try { session->close(); }
            catch (const std::exception &restore) {
                std::cerr << "{\"error\":" << quoted(restore.what()) << "}\n";
            }
        }
        return 1;
    }
}

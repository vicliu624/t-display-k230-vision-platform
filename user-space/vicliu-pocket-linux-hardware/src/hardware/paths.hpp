#pragma once

#include <optional>
#include <string>
#include <vector>

namespace vpl::hardware::paths {

bool exists(const std::string &path);
bool executable(const std::string &path);
std::string read(const std::string &path);
bool write(const std::string &path, const std::string &value);
bool write_atomically(const std::string &path, const std::string &value);
std::optional<long> read_long(const std::string &path);
std::vector<std::string> children(const std::string &path);
bool service_active(const std::string &unit);
int run(const std::vector<std::string> &arguments);
int run_quietly(const std::vector<std::string> &arguments);

}  // namespace vpl::hardware::paths

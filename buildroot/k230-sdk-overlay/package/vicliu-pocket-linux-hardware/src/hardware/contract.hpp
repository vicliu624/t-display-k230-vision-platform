#pragma once

#include <map>
#include <string>

namespace vpl::hardware {

using State = std::map<std::string, std::string>;

State collect_state();
int initialise();
int run_daemon();
int set_control(const std::string &name, const std::string &value);
int get_control(const std::string &name, std::string *value);

}  // namespace vpl::hardware

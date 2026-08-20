#pragma once

#include <string>

namespace vpl::hardware::device_probe {

bool input_name_contains(const std::string &needle);
bool i2c_address_enumerated(unsigned int address);
bool i2c_address_bound_to_driver(unsigned int address, const std::string &driver);
bool class_has_entries(const std::string &directory);

}  // namespace vpl::hardware::device_probe

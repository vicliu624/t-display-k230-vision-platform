#pragma once

#include <map>
#include <string>

namespace vpl::hardware {

using State = std::map<std::string, std::string>;

State collect_state();
int initialise();
int run_daemon();
// Select the board-validated PWM4 mux, including on already-flashed DTBs
// whose malformed pinctrl property did not claim IO52.
bool prepare_keyboard_backlight_route();
// Replace the generic 20 kHz backlight consumer with the board-validated
// low-frequency PWM route that has perceptually distinct keyboard-light
// levels.  The hardware service remains the sole PWM owner afterwards.
bool initialise_keyboard_backlight();
int set_control(const std::string &name, const std::string &value);
int get_control(const std::string &name, std::string *value);

}  // namespace vpl::hardware

#pragma once

#include <string>

namespace vpl::hardware {

struct BatteryReading {
    bool present = false;
    std::string supply_name;
    long capacity_percent = 0;
    long voltage_mv = 0;
    long current_ma = 0;
    long temperature_deci_celsius = 0;
};

struct ChargerReading {
    bool present = false;
    std::string supply_name;
    bool power_good = false;
    std::string charge_state;
};

BatteryReading read_battery_supply();
ChargerReading read_charger_supply();

}  // namespace vpl::hardware

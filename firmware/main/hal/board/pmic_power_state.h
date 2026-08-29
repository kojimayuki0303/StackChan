/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdint>

struct PmicPowerState {
    bool external_power = false;
    bool charging = false;
    bool discharging = false;
};

inline PmicPowerState DecodePmicPowerState(uint8_t power_status)
{
    const uint8_t current_direction = (power_status & 0b01100000) >> 5;
    const bool charging_done = (power_status & 0b00000111) == 0b00000100;
    return {
        current_direction != 2 || charging_done,
        current_direction == 1,
        current_direction == 2 && !charging_done,
    };
}

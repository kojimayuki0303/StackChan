/* SPDX-License-Identifier: MIT */
#include <cstdlib>
#include <iostream>

#include "../main/hal/board/pmic_power_state.h"

namespace {

void expect(bool actual, bool expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void testCharging()
{
    const auto state = DecodePmicPowerState(0b00100000);
    expect(state.external_power, true, "charging external power");
    expect(state.charging, true, "charging flag");
    expect(state.discharging, false, "charging discharging flag");
}

void testDischarging()
{
    const auto state = DecodePmicPowerState(0b01000000);
    expect(state.external_power, false, "discharging external power");
    expect(state.charging, false, "discharging charging flag");
    expect(state.discharging, true, "discharging flag");
}

void testChargeCompleteStillHasExternalPower()
{
    const auto state = DecodePmicPowerState(0b01000100);
    expect(state.external_power, true, "charge complete external power");
    expect(state.discharging, false, "charge complete discharging flag");
}

}  // namespace

int main()
{
    testCharging();
    testDischarging();
    testChargeCompleteStillHasExternalPower();
    return 0;
}

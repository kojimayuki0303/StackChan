/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdlib>
#include <optional>

#include "screen_swipe_direction.h"

// Pure classification of a press/release delta into a swipe direction. Has no
// LVGL/ESP dependencies so it can be exercised directly from host-side unit
// tests (see firmware/tests/screen_swipe_classifier_test.cpp).
//
// Horizontal wins when |dx| > |dy| and |dx| >= minimum_distance.
// Vertical wins when |dy| > |dx| and |dy| >= minimum_distance.
// An exact tie (|dx| == |dy|), or both deltas below the threshold, yields no
// swipe.
inline std::optional<ScreenSwipeDirection> ClassifySwipe(int delta_x, int delta_y, int minimum_distance)
{
    const int abs_x = std::abs(delta_x);
    const int abs_y = std::abs(delta_y);

    if (abs_x > abs_y && abs_x >= minimum_distance) {
        return delta_x < 0 ? ScreenSwipeDirection::Left : ScreenSwipeDirection::Right;
    }
    if (abs_y > abs_x && abs_y >= minimum_distance) {
        return delta_y < 0 ? ScreenSwipeDirection::Up : ScreenSwipeDirection::Down;
    }
    return std::nullopt;
}

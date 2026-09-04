/* SPDX-License-Identifier: MIT */
#pragma once

// Kept in its own header (no LVGL/ESP includes) so both screen_swipe_gesture.h
// and the host-testable screen_swipe_classifier.h can depend on it without
// pulling in firmware-only headers.
enum class ScreenSwipeDirection { Left, Right, Up, Down };

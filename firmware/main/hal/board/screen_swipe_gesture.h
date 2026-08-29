/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdint>
#include <lvgl.h>

enum class ScreenSwipeDirection { Left, Right };

class ScreenSwipeGesture {
public:
    using Callback = void (*)(void* context, ScreenSwipeDirection direction);

    ScreenSwipeGesture(Callback callback, void* context);

    void Attach(lv_obj_t* object);
    bool ShouldSuppressClick() const;

private:
    static constexpr int kMinimumDistance = 50;
    static constexpr uint32_t kClickSuppressionMs = 350;

    Callback callback_ = nullptr;
    void* context_ = nullptr;
    lv_point_t start_point_{};
    bool tracking_ = false;
    uint32_t last_swipe_tick_ = 0;

    static void EventHandler(lv_event_t* event);
    void HandleEvent(lv_event_code_t code);
};

/* SPDX-License-Identifier: MIT */
#include "screen_swipe_gesture.h"

#include <cstdlib>
#include <hal/hal.h>

ScreenSwipeGesture::ScreenSwipeGesture(Callback callback, void* context) : callback_(callback), context_(context)
{
}

void ScreenSwipeGesture::Attach(lv_obj_t* object)
{
    if (object == nullptr) {
        return;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(object, &ScreenSwipeGesture::EventHandler, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(object, &ScreenSwipeGesture::EventHandler, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(object, &ScreenSwipeGesture::EventHandler, LV_EVENT_PRESS_LOST, this);
}

bool ScreenSwipeGesture::ShouldSuppressClick() const
{
    return last_swipe_tick_ != 0 && lv_tick_elaps(last_swipe_tick_) < kClickSuppressionMs;
}

void ScreenSwipeGesture::EventHandler(lv_event_t* event)
{
    auto* self = static_cast<ScreenSwipeGesture*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->HandleEvent(lv_event_get_code(event));
    }
}

void ScreenSwipeGesture::HandleEvent(lv_event_code_t code)
{
    lv_indev_t* touchpad = GetHAL().lvTouchpad;
    if (touchpad == nullptr) {
        return;
    }

    lv_point_t point{};
    lv_indev_get_point(touchpad, &point);
    if (code == LV_EVENT_PRESSED) {
        start_point_ = point;
        tracking_ = true;
        return;
    }
    if ((code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) || !tracking_) {
        return;
    }

    tracking_ = false;
    const int delta_x = point.x - start_point_.x;
    const int delta_y = point.y - start_point_.y;
    if (std::abs(delta_x) < kMinimumDistance || std::abs(delta_x) <= std::abs(delta_y)) {
        return;
    }

    last_swipe_tick_ = lv_tick_get();
    if (callback_ != nullptr) {
        callback_(context_, delta_x < 0 ? ScreenSwipeDirection::Left : ScreenSwipeDirection::Right);
    }
}

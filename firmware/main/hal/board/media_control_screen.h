/* SPDX-License-Identifier: MIT */
#pragma once

#include <array>
#include <atomic>
#include <lvgl.h>

class StackChanMediaScreen {
public:
    StackChanMediaScreen() = default;
    ~StackChanMediaScreen();

    void Setup(lv_obj_t* parent);
    void SetState(bool active, bool playing, const char* title, const char* subtitle);
    bool IsActive() const { return active_; }

private:
    enum class Action { PlayPause, Previous, Next, Chat };
    struct ButtonContext {
        StackChanMediaScreen* screen;
        Action action;
    };
    struct RequestContext {
        StackChanMediaScreen* screen;
        Action action;
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* play_label_ = nullptr;
    bool active_ = false;
    bool playing_ = false;
    std::atomic_bool request_in_progress_{false};
    std::array<ButtonContext, 4> button_contexts_{};

    lv_obj_t* CreateButton(lv_obj_t* parent, ButtonContext* context, const char* text,
                           int x, int y, int width, int height);
    void StartRequest(Action action);
    static void ButtonEvent(lv_event_t* event);
    static void RequestTask(void* arg);
    static const char* ActionPath(Action action);
};

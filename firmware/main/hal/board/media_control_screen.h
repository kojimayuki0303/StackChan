/* SPDX-License-Identifier: MIT */
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <lvgl.h>

#include <display/lvgl_display/lvgl_image.h>

class ScreenSwipeGesture;

class StackChanMediaScreen {
public:
    StackChanMediaScreen() = default;
    ~StackChanMediaScreen();

    void Setup(lv_obj_t* parent, ScreenSwipeGesture* swipe_gesture = nullptr);
    void SetState(bool active, bool playing, const char* title, const char* subtitle);
    void SetVisible(bool visible);
    void SetTextFont(const lv_font_t* font);
    void SetIconFont(const lv_font_t* font);
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
    lv_obj_t* artwork_image_ = nullptr;
    lv_obj_t* artwork_overlay_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* play_label_ = nullptr;
    std::array<lv_obj_t*, 4> action_labels_{};
    std::unique_ptr<LvglImage> artwork_cached_ = nullptr;
    std::string artwork_track_key_;
    std::atomic<uint32_t> artwork_generation_{0};
    bool active_ = false;
    bool playing_ = false;
    std::atomic_bool request_in_progress_{false};
    std::array<ButtonContext, 4> button_contexts_{};
    ScreenSwipeGesture* swipe_gesture_ = nullptr;

    lv_obj_t* CreateButton(lv_obj_t* parent, ButtonContext* context, const char* text,
                           int x, int y, int width, int height);
    void StartRequest(Action action);
    void StartArtworkFetch(uint32_t generation);
    void ClearArtwork();
    static void ButtonEvent(lv_event_t* event);
    static void RequestTask(void* arg);
    static void ArtworkLoaded(void* target, uint32_t generation, std::unique_ptr<LvglImage> image);
    static const char* ActionPath(Action action);
};

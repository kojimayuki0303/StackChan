/* SPDX-License-Identifier: MIT */
#include "media_control_screen.h"
#include "media_artwork_loader.h"
#include "screen_swipe_gesture.h"

#include <application.h>
#include <board.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <font_awesome.h>
#include <memory>
#include <string>

#define TAG "StackChanMediaScreen"

namespace {
constexpr uint32_t kBackground = 0x101418;
constexpr uint32_t kButton = 0x27323A;
constexpr uint32_t kPrimary = 0x1DB954;
constexpr uint32_t kText = 0xF5F7F8;
constexpr uint32_t kMuted = 0x9AA8B2;
}

StackChanMediaScreen::~StackChanMediaScreen()
{
    if (root_ != nullptr) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
}

lv_obj_t* StackChanMediaScreen::CreateButton(lv_obj_t* parent, ButtonContext* context, const char* text,
                                              int x, int y, int width, int height)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(kButton), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, &StackChanMediaScreen::ButtonEvent, LV_EVENT_CLICKED, context);
    if (swipe_gesture_ != nullptr) {
        swipe_gesture_->Attach(button);
    }
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    lv_obj_center(label);
    return label;
}

void StackChanMediaScreen::Setup(lv_obj_t* parent, ScreenSwipeGesture* swipe_gesture)
{
    swipe_gesture_ = swipe_gesture;
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 320, 240);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    if (swipe_gesture_ != nullptr) {
        swipe_gesture_->Attach(root_);
    }

    artwork_image_ = lv_image_create(root_);
    lv_obj_align(artwork_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(artwork_image_, LV_OBJ_FLAG_HIDDEN);

    artwork_overlay_ = lv_obj_create(root_);
    lv_obj_set_size(artwork_overlay_, 320, 240);
    lv_obj_align(artwork_overlay_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(artwork_overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(artwork_overlay_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(artwork_overlay_, 0, 0);
    lv_obj_set_style_radius(artwork_overlay_, 0, 0);
    lv_obj_clear_flag(artwork_overlay_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(root_);
    lv_obj_set_width(title_, 292);
    lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(kText), 0);
    lv_label_set_text(title_, "Spotify");

    subtitle_ = lv_label_create(root_);
    lv_obj_set_width(subtitle_, 292);
    lv_obj_align(subtitle_, LV_ALIGN_TOP_MID, 0, 52);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(subtitle_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(subtitle_, lv_color_hex(kMuted), 0);
    lv_label_set_text(subtitle_, "再生中");

    button_contexts_ = {{{this, Action::Previous}, {this, Action::PlayPause},
                         {this, Action::Next}, {this, Action::Chat}}};
    action_labels_[0] = CreateButton(root_, &button_contexts_[0], FONT_AWESOME_BACKWARD_STEP, 16, 172, 56, 52);
    action_labels_[1] = CreateButton(root_, &button_contexts_[1], FONT_AWESOME_PAUSE, 88, 172, 56, 52);
    play_label_ = action_labels_[1];
    action_labels_[2] = CreateButton(root_, &button_contexts_[2], FONT_AWESOME_FORWARD_STEP, 160, 172, 56, 52);
    action_labels_[3] = CreateButton(root_, &button_contexts_[3], FONT_AWESOME_COMMENT, 232, 172, 56, 52);
    lv_obj_t* chat_label = action_labels_[3];
    lv_obj_set_style_bg_color(lv_obj_get_parent(chat_label), lv_color_hex(kPrimary), 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void StackChanMediaScreen::SetTextFont(const lv_font_t* font)
{
    if (root_ == nullptr || font == nullptr) {
        return;
    }
    lv_obj_set_style_text_font(title_, font, 0);
    lv_obj_set_style_text_font(subtitle_, font, 0);
}

void StackChanMediaScreen::SetIconFont(const lv_font_t* font)
{
    if (root_ == nullptr || font == nullptr) {
        return;
    }
    for (lv_obj_t* label : action_labels_) {
        if (label != nullptr) {
            lv_obj_set_style_text_font(label, font, 0);
        }
    }
}

void StackChanMediaScreen::SetState(bool active, bool playing, const char* title, const char* subtitle)
{
    const bool was_active = active_;
    active_ = active;
    playing_ = playing;
    if (root_ == nullptr) {
        return;
    }
    if (!active) {
        artwork_generation_.fetch_add(1, std::memory_order_relaxed);
        artwork_track_key_.clear();
        ClearArtwork();
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char* display_title = title != nullptr && title[0] != '\0' ? title : "Spotify";
    const char* display_subtitle = subtitle != nullptr && subtitle[0] != '\0'
                                     ? subtitle
                                     : (playing ? "再生中" : "一時停止中");
    lv_label_set_text(title_, display_title);
    lv_label_set_text(subtitle_, display_subtitle);
    lv_label_set_text(play_label_, playing ? FONT_AWESOME_PAUSE : FONT_AWESOME_PLAY);
    const std::string track_key = std::string(display_title) + "\n" + display_subtitle;
    if (!was_active || track_key != artwork_track_key_) {
        artwork_track_key_ = track_key;
        const uint32_t generation = artwork_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
        ClearArtwork();
        StartArtworkFetch(generation);
    }
}

void StackChanMediaScreen::SetVisible(bool visible)
{
    if (root_ == nullptr) {
        return;
    }
    if (visible && active_) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StackChanMediaScreen::ClearArtwork()
{
    if (artwork_image_ != nullptr) {
        lv_obj_add_flag(artwork_image_, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(artwork_image_, nullptr);
    }
    artwork_cached_.reset();
}

void StackChanMediaScreen::StartArtworkFetch(uint32_t generation)
{
    MediaArtworkLoader::Fetch(generation, this, &StackChanMediaScreen::ArtworkLoaded);
}

void StackChanMediaScreen::ArtworkLoaded(
    void* target, uint32_t generation, std::unique_ptr<LvglImage> image)
{
    auto* self = static_cast<StackChanMediaScreen*>(target);
    if (self->artwork_generation_.load(std::memory_order_relaxed) != generation) {
        return;
    }
    if (!lvgl_port_lock(30000)) {
        ESP_LOGW(TAG, "Artwork fetch: failed to lock display");
        return;
    }
    if (self->artwork_generation_.load(std::memory_order_relaxed) == generation &&
        self->artwork_image_ != nullptr) {
        self->artwork_cached_ = std::move(image);
        lv_image_set_src(self->artwork_image_, self->artwork_cached_->image_dsc());
        lv_obj_remove_flag(self->artwork_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(self->artwork_image_);
    }
    lvgl_port_unlock();
}

void StackChanMediaScreen::ButtonEvent(lv_event_t* event)
{
    auto* context = static_cast<ButtonContext*>(lv_event_get_user_data(event));
    if (context != nullptr && context->screen != nullptr &&
        (context->screen->swipe_gesture_ == nullptr ||
         !context->screen->swipe_gesture_->ShouldSuppressClick())) {
        context->screen->StartRequest(context->action);
    }
}

const char* StackChanMediaScreen::ActionPath(Action action)
{
    switch (action) {
        case Action::PlayPause: return "play-pause";
        case Action::Previous: return "previous";
        case Action::Next: return "next";
        case Action::Chat: return "chat";
    }
    return "";
}

void StackChanMediaScreen::StartRequest(Action action)
{
    bool expected = false;
    if (!request_in_progress_.compare_exchange_strong(expected, true)) {
        return;
    }
    auto* context = new RequestContext{this, action};
    if (xTaskCreate(&StackChanMediaScreen::RequestTask, "media_control", 4096, context, 3, nullptr) != pdPASS) {
        delete context;
        request_in_progress_.store(false);
    }
}

void StackChanMediaScreen::RequestTask(void* arg)
{
    std::unique_ptr<RequestContext> context(static_cast<RequestContext*>(arg));
    StackChanMediaScreen* self = context->screen;
    bool success = false;
    do {
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (http == nullptr) {
            break;
        }
        const std::string url = std::string(CONFIG_STACKCHAN_MEDIA_API_URL) + "/" + ActionPath(context->action);
        if (!http->Open("POST", url)) {
            ESP_LOGW(TAG, "Failed to open media control URL: %s", url.c_str());
            break;
        }
        http->Write("", 0);
        const int status = http->GetStatusCode();
        http->Close();
        success = status >= 200 && status < 300;
        if (!success) {
            ESP_LOGW(TAG, "Media control failed: action=%s status=%d", ActionPath(context->action), status);
        }
    } while (false);

    if (success && context->action == Action::Chat) {
        Application::GetInstance().ToggleChatState();
    }
    self->request_in_progress_.store(false);
    vTaskDelete(nullptr);
}

/* SPDX-License-Identifier: MIT */
#include "media_control_screen.h"

#include <application.h>
#include <board.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, &StackChanMediaScreen::ButtonEvent, LV_EVENT_CLICKED, context);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    lv_obj_center(label);
    return label;
}

void StackChanMediaScreen::Setup(lv_obj_t* parent)
{
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, 320, 240);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

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
    action_labels_[0] = CreateButton(root_, &button_contexts_[0], "前へ", 20, 86, 76, 58);
    action_labels_[1] = CreateButton(root_, &button_contexts_[1], "一時停止", 108, 86, 104, 58);
    play_label_ = action_labels_[1];
    action_labels_[2] = CreateButton(root_, &button_contexts_[2], "次へ", 224, 86, 76, 58);
    action_labels_[3] = CreateButton(root_, &button_contexts_[3], "チャットに戻る", 48, 166, 224, 52);
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
    for (lv_obj_t* label : action_labels_) {
        if (label != nullptr) {
            lv_obj_set_style_text_font(label, font, 0);
        }
    }
}

void StackChanMediaScreen::SetState(bool active, bool playing, const char* title, const char* subtitle)
{
    active_ = active;
    playing_ = playing;
    if (root_ == nullptr) {
        return;
    }
    if (!active) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(title_, title != nullptr && title[0] != '\0' ? title : "Spotify");
    lv_label_set_text(subtitle_, subtitle != nullptr && subtitle[0] != '\0'
                                     ? subtitle
                                     : (playing ? "再生中" : "一時停止中"));
    lv_label_set_text(play_label_, playing ? "一時停止" : "再生");
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
}

void StackChanMediaScreen::ButtonEvent(lv_event_t* event)
{
    auto* context = static_cast<ButtonContext*>(lv_event_get_user_data(event));
    if (context != nullptr && context->screen != nullptr) {
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

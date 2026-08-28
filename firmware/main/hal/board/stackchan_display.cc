/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stackchan_display.h"
#include "media_control_screen.h"
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <src/misc/cache/lv_cache.h>
#include <settings.h>
#include <system_info.h>
#include <lvgl.h>
#include <lvgl_theme.h>
#include <stackchan/stackchan.h>
#include <assets/lang_config.h>
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>
#include <board.h>
#include <display/lvgl_display/lvgl_image.h>

using namespace stackchan;
using namespace stackchan::avatar;

#define TAG "StackChanAvatarDisplay"

// How long the dashboard stays down after the last head-pet gesture.
// Slightly longer than HeadPetModifier's own restore delay (3000 ms) so the
// avatar has finished going back to its previous emotion and pose before the
// dashboard covers the face again.
static constexpr uint64_t kDashboardHeadPetRestoreDelayMs = 3500;

// Voice sessions are deliberately user-initiated. The managed firmware config
// disables wake-word detection; both the avatar and dashboard call this helper
// only from an explicit touchscreen click.
static void ToggleChatFromScreenTap()
{
    static uint32_t last_toggle_tick = 0;
    const uint32_t now               = GetHAL().millis();
    if (last_toggle_tick != 0 && now - last_toggle_tick < 2000) {
        return;
    }

    if (hal_bridge::is_xiaozhi_ready()) {
        last_toggle_tick = now;
        hal_bridge::toggle_xiaozhi_chat_state();
    }
}

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

// Have to register themes, so the asset apply can update the text font
void StackChanAvatarDisplay::InitializeLcdThemes()
{
    auto text_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font       = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));        // rgb(255, 255, 255)
    light_theme->set_text_color(lv_color_hex(0x000000));              // rgb(0, 0, 0)
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));   // rgb(224, 224, 224)
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));  // rgb(221, 221, 221)
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));     // rgb(255, 255, 255)
    light_theme->set_system_text_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_border_color(lv_color_hex(0x000000));            // rgb(0, 0, 0)
    light_theme->set_low_battery_color(lv_color_hex(0x000000));       // rgb(0, 0, 0)
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));        // rgb(0, 0, 0)
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));              // rgb(255, 255, 255)
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));   // rgb(31, 31, 31)
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));       // rgb(0, 128, 0)
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));  // rgb(34, 34, 34)
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));     // rgb(0, 0, 0)
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));       // rgb(255, 255, 255)
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));            // rgb(255, 255, 255)
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));       // rgb(255, 0, 0)
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

StackChanAvatarDisplay::StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                                               bool mirror_y, bool swap_xy)
    : LvglDisplay(), panel_io_(panel_io), panel_(panel)
{
    width_  = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_         = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Draw white screen
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // port_cfg.task_priority   = 20;
    port_cfg.task_priority = 3;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle      = panel_io_,
        .panel_handle   = panel_,
        .control_handle = nullptr,
        .buffer_size    = static_cast<uint32_t>(width_ * 20),
        .double_buffer  = false,
        .trans_size     = 0,
        .hres           = static_cast<uint32_t>(width_),
        .vres           = static_cast<uint32_t>(height_),
        .monochrome     = false,
        .rotation =
            {
                .swap_xy  = swap_xy,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma     = 1,
                .buff_spiram  = 0,
                .sw_rotate    = 0,
                .swap_bytes   = 1,
                .full_refresh = 0,
                .direct_mode  = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->SetPreviewImage(nullptr);
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);

    // Idle-screen dashboard: only active when a URL is configured
    dashboard_enabled_ = strlen(CONFIG_STACKCHAN_DASHBOARD_URL) > 0;

    esp_timer_create_args_t dashboard_idle_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->OnDashboardIdleDelayElapsed();
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "dash_idle_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&dashboard_idle_timer_args, &dashboard_idle_timer_);

    esp_timer_create_args_t dashboard_refresh_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->OnDashboardRefreshTimer();
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "dash_refresh_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&dashboard_refresh_timer_args, &dashboard_refresh_timer_);

    esp_timer_create_args_t dashboard_pet_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->OnDashboardPetTimerElapsed();
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "dash_pet_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&dashboard_pet_timer_args, &dashboard_pet_timer_);

    esp_timer_create_args_t dashboard_pet_kick_timer_args = {
        .callback =
            [](void* arg) {
                StackChanAvatarDisplay* display = static_cast<StackChanAvatarDisplay*>(arg);
                display->SuspendDashboardForHeadPet();
            },
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "dash_pet_kick_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&dashboard_pet_kick_timer_args, &dashboard_pet_kick_timer_);

    // Create boot logo label if not warm boot
    if (GetHAL().getWarmRebootTarget() < 0) {
        ESP_LOGI(TAG, "Create boot logo label");
        Lock();
        {
            uitk::lvgl_cpp::ScreenActive screen;
            screen.setBgColor(lv_color_hex(0x000000));
        }
        GetHAL().bootLogo = std::make_unique<BootLogo>();
        Unlock();
    }

    // Robot will be created later in SetupXiaoZhiUI()
}

StackChanAvatarDisplay::~StackChanAvatarDisplay()
{
    ESP_LOGI(TAG, "Destroying StackChanAvatarDisplay");

    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }

    // Invalidate any in-flight dashboard fetch task before tearing down state it
    // would otherwise touch.
    dashboard_generation_.fetch_add(1, std::memory_order_relaxed);

    if (dashboard_idle_timer_ != nullptr) {
        esp_timer_stop(dashboard_idle_timer_);
        esp_timer_delete(dashboard_idle_timer_);
    }
    if (dashboard_refresh_timer_ != nullptr) {
        esp_timer_stop(dashboard_refresh_timer_);
        esp_timer_delete(dashboard_refresh_timer_);
    }
    if (dashboard_pet_kick_timer_ != nullptr) {
        esp_timer_stop(dashboard_pet_kick_timer_);
        esp_timer_delete(dashboard_pet_kick_timer_);
    }
    if (dashboard_pet_timer_ != nullptr) {
        esp_timer_stop(dashboard_pet_timer_);
        esp_timer_delete(dashboard_pet_timer_);
    }
    if (head_pet_signal_connection_ >= 0) {
        GetHAL().onHeadPetGesture.disconnect(head_pet_signal_connection_);
        head_pet_signal_connection_ = -1;
    }
    if (dashboard_image_ != nullptr) {
        lv_obj_del(dashboard_image_);
    }
    media_screen_.reset();

    auto& stackchan = GetStackChan();
    if (stackchan.hasAvatar()) {
        stackchan.resetAvatar();
    }
}

bool StackChanAvatarDisplay::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void StackChanAvatarDisplay::Unlock()
{
    lvgl_port_unlock();
}

lv_disp_t* StackChanAvatarDisplay::GetLvglDisplay()
{
    return display_;
}

#include <hal/board/hal_bridge.h>

void StackChanAvatarDisplay::SetupUI()
{
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called

    auto& stackchan = GetStackChan();

    if (stackchan.hasAvatar()) {
        ESP_LOGW(TAG, "Avatar already created");
        return;
    }

    DisplayLockGuard lock(this);

    ESP_LOGI(TAG, "Creating Stack-chan Avatar...");

    auto avatar = std::make_unique<DefaultAvatar>();
    avatar->init(lv_screen_active());
    avatar->getPanel()->onClick().connect([]() { ToggleChatFromScreenTap(); });

    stackchan.attachAvatar(std::move(avatar));
    stackchan.addModifier(std::make_unique<BreathModifier>());
    blink_modifier_id_ = stackchan.addModifier(std::make_unique<BlinkModifier>());
    stackchan.addModifier(std::make_unique<HeadPetModifier>());
    stackchan.addModifier(std::make_unique<ImuEventModifier>());

    preview_image_ = lv_image_create(lv_screen_active());
    lv_obj_set_size(preview_image_, 320, 240);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    dashboard_image_ = lv_image_create(lv_screen_active());
    lv_obj_set_size(dashboard_image_, 320, 240);
    lv_obj_align(dashboard_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(dashboard_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        dashboard_image_, [](lv_event_t*) { ToggleChatFromScreenTap(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(dashboard_image_, LV_OBJ_FLAG_HIDDEN);

    media_screen_ = std::make_unique<StackChanMediaScreen>();
    media_screen_->Setup(lv_screen_active());
    if (current_theme_ != nullptr) {
        auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
        media_screen_->SetTextFont(lvgl_theme->text_font()->font());
    }
    media_screen_->SetIconFont(&font_awesome_30_4);

    // Take the dashboard down while the head is being petted so the stock
    // HeadPetModifier reaction (happy face + heart/shy decorators) is not
    // covered by it. Only the actual petting gestures count: a bare Press is
    // ignored because it does not trigger the reaction either.
    head_pet_signal_connection_ = GetHAL().onHeadPetGesture.connect([this](HeadPetGesture gesture) {
        const bool petting = (gesture == HeadPetGesture::SwipeForward || gesture == HeadPetGesture::SwipeBackward);
        // A Release restarts the restore delay (the same way HeadPetModifier
        // restarts its own), but only once petting has actually taken the
        // dashboard down.
        const bool petting_ended =
            (gesture == HeadPetGesture::Release && dashboard_pet_suspended_.load(std::memory_order_relaxed));
        if (!petting && !petting_ended) {
            return;
        }
        if (!dashboard_enabled_ || dashboard_pet_kick_timer_ == nullptr) {
            return;
        }

        // Must not block here: uitk::Signal::emit() holds its mutex across this
        // call, so waiting on the LVGL lock would stall the head-touch task and
        // starve every other slot - HeadPetModifier included. Hand the work to
        // the esp_timer task instead. Return values are ignored on purpose: a
        // kick that is already queued does the same job.
        esp_timer_stop(dashboard_pet_kick_timer_);
        esp_timer_start_once(dashboard_pet_kick_timer_, 0);
    });

    // GetHAL().startStackChanAutoUpdate(24);

    auto config        = hal_bridge::get_xiaozhi_config();
    idle_motion_level_ = config.idleRandomMovementLevel;

    ESP_LOGI(TAG, "Avatar created and started");
}

void StackChanAvatarDisplay::LvglLock()
{
    if (!Lock(30000)) {
        ESP_LOGE("Display", "Failed to lock display");
    }
}

void StackChanAvatarDisplay::LvglUnlock()
{
    Unlock();
}

void StackChanAvatarDisplay::CreateIdleMotionModifier()
{
    auto& stackchan = GetStackChan();

    switch (idle_motion_level_) {
        case 0:
            idle_motion_modifier_id_ = -1;
            return;
        case 1:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(8000, 12000));
            return;
        case 3:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>(2000, 4000));
            return;
        case 2:
        default:
            idle_motion_modifier_id_ = stackchan.addModifier(std::make_unique<IdleMotionModifier>());
            return;
    }
}

void StackChanAvatarDisplay::SetEmotion(const char* emotion)
{
    auto& stackchan = GetStackChan();

    if (!stackchan.hasAvatar() || !emotion) {
        return;
    }

    DisplayLockGuard lock(this);

    // ESP_LOGE(TAG, "SetEmotion: %s", emotion);

    auto& avatar = stackchan.avatar();

    // Map emotion string to stackchan::Emotion
    if (strcmp(emotion, "neutral") == 0) {
        avatar.setEmotion(Emotion::Neutral);
    } else if (strcmp(emotion, "happy") == 0) {
        avatar.setEmotion(Emotion::Happy);
    } else if (strcmp(emotion, "laughing") == 0) {
        avatar.setEmotion(Emotion::Happy);
    } else if (strcmp(emotion, "angry") == 0) {
        avatar.setEmotion(Emotion::Angry);
    } else if (strcmp(emotion, "sad") == 0) {
        avatar.setEmotion(Emotion::Sad);
    } else if (strcmp(emotion, "crying") == 0) {
        avatar.setEmotion(Emotion::Sad);
    } else if (strcmp(emotion, "sleepy") == 0) {
        avatar.setEmotion(Emotion::Sleepy);
        avatar.setSpeech("Zzz…");
        is_sleeping_ = true;
        // avatar.mouth().setWeight(10);

        // Stop idle motion
        ESP_LOGW(TAG, "Stop idle motion");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
            stackchan.removeModifier(idle_expression_modifier_id_);
            idle_expression_modifier_id_ = -1;
        }

        // Return to default pose
        auto& motion = GetStackChan().motion();
        motion.pitchServo().moveWithSpeed(0, 80);

    } else if (strcmp(emotion, "doubtful") == 0) {
        avatar.setEmotion(Emotion::Doubt);
    } else {
        ESP_LOGW(TAG, "Unknown emotion: %s, using NEUTRAL", emotion);
        avatar.setEmotion(Emotion::Neutral);
    }

    // Resync blink modifier base eye weights
    auto blink_modifier = static_cast<BlinkModifier*>(stackchan.getModifier(blink_modifier_id_));
    if (blink_modifier) {
        blink_modifier->resyncEyeWeights();
    }
}

void StackChanAvatarDisplay::SetChatMessage(const char* role, const char* content)
{
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!", role, content);
    }

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    // ESP_LOGE(TAG, "SetChatMessage: role=%s, content=%s", role ? role : "null", content ? content : "null");

    DisplayLockGuard lock(this);

    if (strcmp(role, "system") == 0) {
        stackchan.avatar().setSpeech(content);
    } else if (strcmp(role, "assistant") == 0) {
        stackchan.avatar().setSpeech(content);
    }
}

void StackChanAvatarDisplay::ClearChatMessages()
{
    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        return;
    }

    DisplayLockGuard lock(this);

    stackchan.avatar().clearSpeech();

    ESP_LOGI(TAG, "Chat messages cleared");
}

void StackChanAvatarDisplay::SetMediaPlayback(bool active, bool playing, const char* title, const char* subtitle)
{
    if (active) {
        HideDashboard();
    }
    DisplayLockGuard lock(this);
    if (media_screen_ != nullptr) {
        media_screen_->SetState(active, playing, title, subtitle);
    }
    if (active) {
        // Spotify uses the same audio decoder path as assistant speech, but
        // blue would misleadingly look like an open conversation. Media mode
        // has its own screen, so keep the body lamp off.
        GetHAL().setRgbColor(0, 0, 0, 0);
        GetHAL().refreshRgb();
    }
}

void StackChanAvatarDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image)
{
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc          = preview_image_cached_->image_dsc();
    // Set image source and show preview image
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // Scale to fit width
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(preview_image_);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, 6000 * 1000));
}

/* -------------------------------------------------------------------------- */
/*                             Idle-screen dashboard                          */
/* -------------------------------------------------------------------------- */
//
// State machine:
//   idle enter (SetStatus(STANDBY)) -> StartDashboardIdleWatch()
//       arms a one-shot timer for CONFIG_STACKCHAN_DASHBOARD_IDLE_DELAY_SECONDS.
//   idle-delay timer fires -> OnDashboardIdleDelayElapsed()
//       if still idle: kicks off the first fetch and arms the periodic
//       refresh timer (CONFIG_STACKCHAN_DASHBOARD_REFRESH_SECONDS).
//   refresh timer fires -> OnDashboardRefreshTimer()
//       if still idle and no fetch already in flight: kicks off another fetch.
//   any idle exit (SetStatus() with a non-STANDBY status), SetPowerSaveMode(true),
//   or destruction -> HideDashboard()
//       hides the image instantly, stops both timers, and bumps
//       dashboard_generation_ so a fetch that is already in flight discards
//       its result instead of popping the dashboard back up after wake.

void StackChanAvatarDisplay::StartDashboardIdleWatch()
{
    if (!dashboard_enabled_ || dashboard_watch_active_) {
        return;
    }

    dashboard_watch_active_ = true;
    esp_timer_stop(dashboard_idle_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(
        dashboard_idle_timer_, static_cast<uint64_t>(CONFIG_STACKCHAN_DASHBOARD_IDLE_DELAY_SECONDS) * 1000000ULL));
}

void StackChanAvatarDisplay::HideDashboard()
{
    StopDashboardWatch();
    DisplayLockGuard lock(this);
    HideDashboardVisualLocked();
}

void StackChanAvatarDisplay::StopDashboardWatch()
{
    // Bump first so any fetch task currently running (or about to run) sees a
    // stale generation and discards its result under lock instead of showing
    // the dashboard after we've already left idle.
    dashboard_generation_.fetch_add(1, std::memory_order_relaxed);
    dashboard_watch_active_ = false;

    if (dashboard_idle_timer_ != nullptr) {
        esp_timer_stop(dashboard_idle_timer_);
    }
    if (dashboard_refresh_timer_ != nullptr) {
        esp_timer_stop(dashboard_refresh_timer_);
    }
    if (dashboard_pet_kick_timer_ != nullptr) {
        esp_timer_stop(dashboard_pet_kick_timer_);
    }
    if (dashboard_pet_timer_ != nullptr) {
        esp_timer_stop(dashboard_pet_timer_);
    }
    dashboard_pet_suspended_.store(false, std::memory_order_relaxed);
}

void StackChanAvatarDisplay::HideDashboardVisualLocked()
{
    if (dashboard_image_ == nullptr) {
        return;
    }
    lv_obj_add_flag(dashboard_image_, LV_OBJ_FLAG_HIDDEN);
    dashboard_image_cached_.reset();
}

void StackChanAvatarDisplay::OnDashboardIdleDelayElapsed()
{
    if (!dashboard_watch_active_ || !hal_bridge::is_xiaozhi_idle()) {
        return;
    }

    StartDashboardFetch();

    esp_timer_stop(dashboard_refresh_timer_);
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        dashboard_refresh_timer_, static_cast<uint64_t>(CONFIG_STACKCHAN_DASHBOARD_REFRESH_SECONDS) * 1000000ULL));
}

void StackChanAvatarDisplay::OnDashboardRefreshTimer()
{
    if (!dashboard_watch_active_ || !hal_bridge::is_xiaozhi_idle()) {
        return;
    }

    if (dashboard_fetch_in_progress_.load(std::memory_order_relaxed)) {
        ESP_LOGW(TAG, "Dashboard fetch still in progress, skipping this refresh tick");
        return;
    }

    StartDashboardFetch();
}

void StackChanAvatarDisplay::StartDashboardFetch()
{
    bool expected = false;
    if (!dashboard_fetch_in_progress_.compare_exchange_strong(expected, true)) {
        // A fetch is already running.
        return;
    }

    auto* ctx      = new DashboardFetchContext{this, dashboard_generation_.load(std::memory_order_relaxed)};
    BaseType_t res = xTaskCreate(&StackChanAvatarDisplay::DashboardFetchTask, "dash_fetch", 6144, ctx, 3, nullptr);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dashboard fetch task");
        delete ctx;
        dashboard_fetch_in_progress_.store(false, std::memory_order_relaxed);
    }
}

void StackChanAvatarDisplay::SuspendDashboardForHeadPet()
{
    if (!dashboard_enabled_ || !dashboard_watch_active_ || dashboard_pet_timer_ == nullptr) {
        return;
    }

    if (!dashboard_pet_suspended_.exchange(true, std::memory_order_relaxed)) {
        // First gesture of this petting session: uncover the face. The decoded
        // frame is kept in dashboard_image_cached_ so it can be put back up
        // without another fetch.
        DisplayLockGuard lock(this);
        if (dashboard_image_ != nullptr) {
            lv_obj_add_flag(dashboard_image_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Re-arm on every gesture so the dashboard only returns once the petting
    // has actually stopped.
    esp_timer_stop(dashboard_pet_timer_);
    esp_timer_start_once(dashboard_pet_timer_, kDashboardHeadPetRestoreDelayMs * 1000ULL);
}

void StackChanAvatarDisplay::OnDashboardPetTimerElapsed()
{
    dashboard_pet_suspended_.store(false, std::memory_order_relaxed);

    if (!dashboard_watch_active_ || !hal_bridge::is_xiaozhi_idle()) {
        // Left idle while being petted - SetStatus() already hid the dashboard.
        return;
    }

    {
        DisplayLockGuard lock(this);
        if (dashboard_image_ != nullptr && dashboard_image_cached_ != nullptr) {
            lv_obj_remove_flag(dashboard_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(dashboard_image_);
            return;
        }
    }

    // No cached frame (never fetched, or it was dropped) - pull a fresh one.
    StartDashboardFetch();
}

void StackChanAvatarDisplay::DashboardFetchTask(void* arg)
{
    std::unique_ptr<DashboardFetchContext> ctx(static_cast<DashboardFetchContext*>(arg));
    StackChanAvatarDisplay* self = ctx->self;
    const uint32_t generation    = ctx->generation;

    do {
        auto network = Board::GetInstance().GetNetwork();
        auto http    = network->CreateHttp(0);
        if (http == nullptr) {
            ESP_LOGW(TAG, "Dashboard fetch: failed to create HTTP client");
            break;
        }

        const std::string url = CONFIG_STACKCHAN_DASHBOARD_URL;
        Settings websocket_settings("websocket", false);
        std::string token = websocket_settings.GetString("token");
        if (!token.empty()) {
            if (token.find(" ") == std::string::npos) {
                token = "Bearer " + token;
            }
            http->SetHeader("Authorization", token.c_str());
        }
        http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
        if (!http->Open("GET", url)) {
            ESP_LOGW(TAG, "Dashboard fetch: failed to open %s", url.c_str());
            break;
        }

        int status_code = http->GetStatusCode();
        if (status_code != 200) {
            ESP_LOGW(TAG, "Dashboard fetch: unexpected status code %d", status_code);
            http->Close();
            break;
        }

        size_t content_length = http->GetBodyLength();
        if (content_length == 0) {
            ESP_LOGW(TAG, "Dashboard fetch: empty body");
            http->Close();
            break;
        }

        uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr) {
            data = static_cast<uint8_t*>(heap_caps_malloc(content_length, MALLOC_CAP_8BIT));
        }
        if (data == nullptr) {
            ESP_LOGW(TAG, "Dashboard fetch: failed to allocate %u bytes", (unsigned)content_length);
            http->Close();
            break;
        }

        size_t total_read = 0;
        bool read_failed   = false;
        while (total_read < content_length) {
            int ret = http->Read(reinterpret_cast<char*>(data) + total_read, content_length - total_read);
            if (ret < 0) {
                ESP_LOGW(TAG, "Dashboard fetch: read error");
                read_failed = true;
                break;
            }
            if (ret == 0) {
                break;
            }
            total_read += ret;
        }
        http->Close();

        if (read_failed || total_read == 0) {
            heap_caps_free(data);
            break;
        }

        std::unique_ptr<LvglImage> image;
        try {
            image = std::make_unique<LvglAllocatedImage>(data, total_read);
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "Dashboard fetch: failed to decode image: %s", e.what());
            heap_caps_free(data);
            break;
        }

        // Discard the result if the dashboard was hidden (idle exited, power
        // save entered, or the display is being destroyed) while we were
        // fetching.
        if (self->dashboard_generation_.load(std::memory_order_relaxed) != generation) {
            ESP_LOGI(TAG, "Dashboard fetch: discarding stale result (no longer idle)");
            break;
        }

        DisplayLockGuard lock(self);
        // Re-check under lock: HideDashboard() may have bumped the generation
        // just before we acquired it.
        if (self->dashboard_generation_.load(std::memory_order_relaxed) != generation ||
            self->dashboard_image_ == nullptr) {
            break;
        }

        self->dashboard_image_cached_ = std::move(image);
        auto img_dsc                  = self->dashboard_image_cached_->image_dsc();
        lv_image_set_src(self->dashboard_image_, img_dsc);
        if (img_dsc->header.w > 0) {
            // Scale to fit width, same convention as SetPreviewImage().
            lv_image_set_scale(self->dashboard_image_, 256 * self->width_ / img_dsc->header.w);
        }
        if (self->dashboard_pet_suspended_.load(std::memory_order_relaxed)) {
            // The head is being petted right now: keep the freshly decoded
            // frame cached but leave the face visible. OnDashboardPetTimerElapsed()
            // will put it back up once the petting is over.
            break;
        }
        lv_obj_remove_flag(self->dashboard_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->dashboard_image_);
    } while (false);

    self->dashboard_fetch_in_progress_.store(false, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

void StackChanAvatarDisplay::UpdateStatusBar(bool update_all)
{
}

void StackChanAvatarDisplay::SetTheme(Theme* theme)
{
    ESP_LOGI(TAG, "SetTheme: %s", theme->name().c_str());

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font  = lvgl_theme->text_font()->font();

    stackchan.avatar().setSpeechTextFont((void*)text_font);
    if (media_screen_ != nullptr) {
        media_screen_->SetTextFont(text_font);
    }
}

static bool _is_xiaozhi_ready = false;
static bool _is_xiaozhi_idle  = false;
bool hal_bridge::is_xiaozhi_ready()
{
    return _is_xiaozhi_ready;
}
bool hal_bridge::is_xiaozhi_idle()
{
    return _is_xiaozhi_idle;
}

void StackChanAvatarDisplay::SetStatus(const char* status)
{
    // ESP_LOGE(TAG, "SetStatus: %s", status);

    auto& stackchan = GetStackChan();
    if (!stackchan.hasAvatar()) {
        ESP_LOGE(TAG, "Avatar is invalid");
        return;
    }

    auto& avatar = stackchan.avatar();
    auto& motion = stackchan.motion();

    DisplayLockGuard lock(this);

    bool is_idle      = false;
    bool is_listening = false;

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        if (speaking_modifier_id_ >= 0) {
            // Start speaking
            stackchan.removeModifier(speaking_modifier_id_);
            avatar.mouth().setWeight(0);
            speaking_modifier_id_ = -1;
        }

        GetHAL().setRgbColor(0, 0, 50, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        _is_xiaozhi_ready = true;

        if (speaking_modifier_id_ >= 0) {
            // Stop speaking
            stackchan.removeModifier(speaking_modifier_id_);
            avatar.mouth().setWeight(0);
            speaking_modifier_id_ = -1;
        }

        is_idle = true;

        GetHAL().setRgbColor(0, 0, 0, 0);
        GetHAL().refreshRgb();

    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        if (speaking_modifier_id_ < 0) {
            speaking_modifier_id_ = stackchan.addModifier(std::make_unique<SpeakingModifier>(0, 180, false));
        }

        if (media_screen_ != nullptr && media_screen_->IsActive()) {
            GetHAL().setRgbColor(0, 0, 0, 0);
        } else {
            GetHAL().setRgbColor(0, 0, 0, 50);
        }
        GetHAL().refreshRgb();
    } else {
        avatar.setSpeech(status);
    }

    if (is_idle) {
        // Start idle motion
        ESP_LOGW(TAG, "Start idle motion");
        if (idle_motion_modifier_id_ < 0) {
            if (idle_motion_level_ > 0) {
                CreateIdleMotionModifier();
            }
            idle_expression_modifier_id_ = stackchan.addModifier(std::make_unique<IdleExpressionModifier>());
        }

        _is_xiaozhi_idle = true;

        // Start (or leave running) the idle-screen dashboard watch.
        StartDashboardIdleWatch();
    } else {
        // Stop idle motion
        ESP_LOGW(TAG, "Stop idle motion");
        if (idle_motion_modifier_id_ >= 0) {
            stackchan.removeModifier(idle_motion_modifier_id_);
            idle_motion_modifier_id_ = -1;
            stackchan.removeModifier(idle_expression_modifier_id_);
            idle_expression_modifier_id_ = -1;
        }

        // if (!is_listening) {
        //     // Return to default pose
        //     motion.pitchServo().moveWithSpeed(200, 350);
        //     motion.yawServo().moveWithSpeed(0, 350);
        // }

        _is_xiaozhi_idle = false;

        // Any non-idle status (wake word / listening / connecting / speaking, etc.)
        // means the user is interacting - hide the dashboard instantly so the
        // avatar face returns.
        // SetStatus already owns the LVGL lock. Stop the timers/state first,
        // then hide the visual without attempting a nested lvgl_port_lock.
        StopDashboardWatch();
        HideDashboardVisualLocked();
    }

    // Clear sleep state
    if (is_sleeping_) {
        avatar.setSpeech("");
    }
}

void StackChanAvatarDisplay::ShowNotification(const char* notification, int duration_ms)
{
}

void StackChanAvatarDisplay::SetPowerSaveMode(bool on)
{
    if (on) {
        // Screen is about to go dark (or the assistant is being shut down) -
        // stop fetching/showing the dashboard until we wake back up.
        HideDashboard();
    } else if (hal_bridge::is_xiaozhi_idle()) {
        // Waking up while still idle (no SetStatus() transition happens on
        // its own here) - resume the idle-delay watch so the dashboard comes
        // back after the usual delay.
        StartDashboardIdleWatch();
    }

    LvglDisplay::SetPowerSaveMode(on);
}

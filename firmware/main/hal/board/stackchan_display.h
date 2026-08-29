/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <display/lvgl_display/lvgl_display.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <atomic>
#include <memory>
#include "media_control_screen.h"
#include "screen_swipe_gesture.h"

class StackChanAvatarDisplay : public LvglDisplay {
private:
    enum class DisplayMode { Spotify, Codex, Dashboard };

    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_       = nullptr;
    int speaking_modifier_id_           = -1;
    int idle_motion_modifier_id_        = -1;
    int idle_expression_modifier_id_    = -1;
    int blink_modifier_id_              = -1;
    bool is_sleeping_                   = false;
    uint8_t idle_motion_level_          = 2;

    lv_obj_t* preview_image_                         = nullptr;
    esp_timer_handle_t preview_timer_                = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    std::unique_ptr<StackChanMediaScreen> media_screen_ = nullptr;
    std::unique_ptr<ScreenSwipeGesture> screen_swipe_gesture_ = nullptr;
    DisplayMode display_mode_ = DisplayMode::Dashboard;
    bool manual_display_mode_ = false;

    // Idle-screen dashboard: shows a server-rendered PNG full-screen over the
    // avatar after the assistant has been idle for a while. See
    // StartDashboardIdleWatch()/HideDashboard() for the state machine.
    lv_obj_t* dashboard_image_                         = nullptr;
    esp_timer_handle_t dashboard_idle_timer_           = nullptr;  // one-shot: idle delay before first fetch
    esp_timer_handle_t dashboard_refresh_timer_        = nullptr;  // periodic: refetch while shown
    std::unique_ptr<LvglImage> dashboard_image_cached_ = nullptr;
    std::atomic<uint32_t> dashboard_generation_{0};  // bumped whenever the dashboard is hidden; lets an
                                                      // in-flight fetch discard a stale result after wake
    std::atomic<bool> dashboard_fetch_in_progress_{false};
    bool dashboard_watch_active_ = false;  // idle-delay timer pending or dashboard currently shown/refreshing
    bool dashboard_enabled_      = false;  // true when CONFIG_STACKCHAN_DASHBOARD_URL is non-empty

    // Head-pet reaction: while the head is being petted the dashboard is
    // temporarily taken down so the stock (factory) reaction - happy face,
    // heart/shy decorators and the head motion - is visible on its own. The
    // dashboard comes back shortly after the petting stops. See
    // SuspendDashboardForHeadPet().
    // NOTE: the head-pet signal is emitted from the head-touch task while
    // uitk::Signal holds its own mutex, so the slot must never block. It only
    // kicks dashboard_pet_kick_timer_; all LVGL work happens on the esp_timer
    // task, the same way preview_timer_ does.
    esp_timer_handle_t dashboard_pet_kick_timer_ = nullptr;  // one-shot 0 ms: hand the gesture off the touch task
    esp_timer_handle_t dashboard_pet_timer_      = nullptr;  // one-shot: restore delay after the last pet gesture
    std::atomic<bool> dashboard_pet_suspended_{false};
    int head_pet_signal_connection_ = -1;

    void CreateIdleMotionModifier();

    void StartDashboardIdleWatch();
    void HideDashboard();
    void StopDashboardWatch();
    void HideDashboardVisualLocked();
    void OnDashboardIdleDelayElapsed();
    void OnDashboardRefreshTimer();
    void StartDashboardFetch();
    struct DashboardFetchContext {
        StackChanAvatarDisplay* self;
        uint32_t generation;
        DisplayMode display_mode;
        bool manual_display_mode;
    };
    static void DashboardFetchTask(void* arg);
    void SuspendDashboardForHeadPet();
    void OnDashboardPetTimerElapsed();
    void ApplyDisplayModeLocked();
    void ShowManualDashboardLocked();
    void OnScreenSwipe(ScreenSwipeDirection direction);
    static void ScreenSwipeCallback(void* context, ScreenSwipeDirection direction);
    static void DashboardEventHandler(lv_event_t* event);

protected:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    StackChanAvatarDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
                           int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    virtual ~StackChanAvatarDisplay();

    void InitializeLcdThemes();

    // Override Display methods to control Robot
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetMediaPlayback(bool active, bool playing, const char* title, const char* subtitle) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetupUI() override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void SetPowerSaveMode(bool on) override;

    void LvglLock();
    void LvglUnlock();
    lv_disp_t* GetLvglDisplay();
};

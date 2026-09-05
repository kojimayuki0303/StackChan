#include "sdkconfig.h"
#if CONFIG_STACKCHAN_USB_SPEAKER
#include "usb_speaker_diagnostics.h"
#include <atomic>
#include <tusb.h>
#include <esp_ota_ops.h>
#include <hal/wdt_hal.h>
#include <soc/rtc.h>
#include <esp_private/rtc_clk.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
std::atomic<uint32_t> calls{0}, received{0}, played{0}, nonzero{0}, errors{0}, dropped{0};
uint32_t snapshot[7];
uint32_t boot_snapshot[2];
std::atomic<bool> rollback_started{false};

void Rollback(void*)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    // The old valid OTA slot is preserved during USB feature installation.
    // If it is unavailable the API returns an error and leaves this app running.
    if (esp_ota_mark_app_invalid_rollback() == ESP_OK) {
        tud_disconnect();
        // CPU/digital-system resets preserve RTC USB PHY state on S3.
        // Reset the RTC domain too, as esptool's watchdog_reset does, so the
        // previous serial firmware can enumerate again on the same cable.
        wdt_hal_context_t watchdog;
        wdt_hal_init(&watchdog, WDT_RWDT, 0, false);
        wdt_hal_write_protect_disable(&watchdog);
        wdt_hal_config_stage(&watchdog, WDT_STAGE0, rtc_clk_slow_freq_get_hz() / 10,
                             WDT_STAGE_ACTION_RESET_RTC);
        wdt_hal_enable(&watchdog);
        wdt_hal_write_protect_enable(&watchdog);
        for (;;) vTaskDelay(pdMS_TO_TICKS(100));
    }
    rollback_started.store(false);
    vTaskDelete(nullptr);
}
}

void RecordUsbSpeakerWrite(const int16_t* pcm, size_t frames, int written)
{
    calls.fetch_add(1);
    received.fetch_add(frames);
    if (written < 0) { errors.fetch_add(1); return; }
    if (written == 0) { dropped.fetch_add(frames); return; }
    played.fetch_add(written);
    for (size_t i = 0; i < static_cast<size_t>(written); ++i) {
        if (pcm[i] != 0) { nonzero.fetch_add(1); break; }
    }
}

// Local USB diagnostics plus an explicit host command to restore the previous
// valid firmware. No PCM, microphone, network, or sound-triggered actions.
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                          tusb_control_request_t const* request)
{
    if (request->bmRequestType == 0xc0 && request->bRequest == 0x52 &&
        request->wValue == 0x5043 && request->wIndex == 0 && request->wLength == sizeof(boot_snapshot)) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        const auto* running = esp_ota_get_running_partition();
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return false;
        boot_snapshot[0] = running->subtype;
        boot_snapshot[1] = state;
        return tud_control_xfer(rhport, request, boot_snapshot, sizeof(boot_snapshot));
    }
    if (request->bmRequestType == 0x40 && request->bRequest == 0x51 &&
        request->wValue == 0x5043 && request->wIndex == 0 && request->wLength == 0) {
        if (stage == CONTROL_STAGE_SETUP) return tud_control_status(rhport, request);
        if (stage == CONTROL_STAGE_ACK && !rollback_started.exchange(true)) {
            if (xTaskCreate(Rollback, "usb_restore", 4096, nullptr, 5, nullptr) != pdPASS) {
                rollback_started.store(false);
            }
        }
        return true;
    }
    if (request->bmRequestType != 0xc0 || request->bRequest != 0x50 ||
        request->wValue != 0x5043 || request->wIndex != 0 || request->wLength != sizeof(snapshot)) {
        return false;
    }
    if (stage != CONTROL_STAGE_SETUP) return true;
    snapshot[0] = 1;
    snapshot[1] = calls.load();
    snapshot[2] = received.load();
    snapshot[3] = played.load();
    snapshot[4] = nonzero.load();
    snapshot[5] = errors.load();
    snapshot[6] = dropped.load();
    return tud_control_xfer(rhport, request, snapshot, sizeof(snapshot));
}
#endif

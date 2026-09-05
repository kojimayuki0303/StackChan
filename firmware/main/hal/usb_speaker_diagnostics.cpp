#include "sdkconfig.h"
#if CONFIG_STACKCHAN_USB_SPEAKER
#include "usb_speaker_diagnostics.h"
#include <atomic>
#include <tusb.h>
#include <esp_ota_ops.h>
#include <esp_rom_sys.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
std::atomic<uint32_t> calls{0}, received{0}, played{0}, nonzero{0}, errors{0}, dropped{0};
uint32_t snapshot[7];
std::atomic<bool> rollback_started{false};

void Rollback(void*)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    // The old valid OTA slot is preserved during USB feature installation.
    // If it is unavailable the API returns an error and leaves this app running.
    if (esp_ota_mark_app_invalid_rollback() == ESP_OK) {
        tud_disconnect();
        // esp_restart() preserves USB peripheral state on S3. Hand the PHY
        // back to hardware Serial/JTAG, then reset the whole digital system
        // so the previous serial firmware can enumerate without a button press.
        REG_CLR_BIT(RTC_CNTL_USB_CONF_REG,
                    RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
        esp_rom_software_reset_system();
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

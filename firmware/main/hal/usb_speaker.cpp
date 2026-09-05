#include "usb_speaker.h"
#include "sdkconfig.h"

#if CONFIG_STACKCHAN_USB_SPEAKER
#include "usb_speaker_pcm.h"
#include "usb_speaker_diagnostics.h"
#include "board/cores3_audio_codec.h"
#include <board.h>
#include <usb_device_uac.h>
#include <esp_log.h>
#include <array>
#include <atomic>

namespace {
// Match the UAC descriptor's initial 0 dB setting. Mac volume is independent
// of the saved native speech volume; it must not rewrite the latter's NVS.
std::atomic<unsigned> volume{100};
std::atomic<bool> muted{false};

esp_err_t Output(uint8_t* bytes, size_t length, void* context)
{
    auto* codec = static_cast<CoreS3AudioCodec*>(context);
    // Fixed-size chunks bound stack use and allow native speech between writes.
    std::array<int16_t, 240> pcm{};
    if (length % 4 != 0) return ESP_ERR_INVALID_SIZE;
    while (length > 0) {
        const size_t frames = usb_speaker::StereoToMono(bytes, length, pcm.data(), pcm.size(),
                                                       volume.load(), muted.load());
        const int written = codec->WriteUsbPcm(pcm.data(), frames);
        RecordUsbSpeakerWrite(pcm.data(), frames, written);
        bytes += frames * 4;
        length -= frames * 4;
    }
    return ESP_OK;
}
} // namespace
#endif

void StartUsbSpeaker()
{
#if CONFIG_STACKCHAN_USB_SPEAKER
    static_assert(CONFIG_UAC_MIC_CHANNEL_NUM == 0, "USB microphone must remain disabled");
    static_assert(CONFIG_UAC_SAMPLE_RATE == 24000 && CONFIG_UAC_SPEAKER_CHANNEL_NUM == 2,
                  "USB PCM format must match the speaker converter");
    auto* codec = static_cast<CoreS3AudioCodec*>(Board::GetInstance().GetAudioCodec());
    codec->Start();
    uac_device_config_t config{};
    config.output_cb = Output;
    config.set_mute_cb = [](uint32_t value, void*) { muted.store(value != 0); };
    config.set_volume_cb = [](uint32_t value, void*) { volume.store(std::min<uint32_t>(value, 100)); };
    config.cb_ctx = codec;
    const esp_err_t result = uac_device_init(&config);
    ESP_LOGI("UsbSpeaker", "USB playback initialization: %s", esp_err_to_name(result));
#endif
}

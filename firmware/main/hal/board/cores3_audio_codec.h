#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>

class CoreS3AudioCodec : public AudioCodec {
private:
    std::recursive_mutex output_mutex_;
    uint32_t last_native_write_ms_ = 0;
    uint32_t last_usb_write_ms_ = 0;
    bool native_write_seen_ = false;
    bool usb_write_seen_ = false;
    int WriteOutput(const int16_t* data, int samples);
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;

    // Lightweight output-path telemetry.  These counters make an otherwise
    // silent codec/I2S failure distinguishable from an upstream no-data
    // failure in the serial log without dumping PCM or running a watchdog.
    uint64_t output_write_count_ = 0;
    uint64_t output_nonzero_write_count_ = 0;
    uint64_t output_write_error_count_ = 0;
    uint64_t output_disabled_write_count_ = 0;
    uint64_t output_empty_write_count_ = 0;
    uint64_t output_frames_written_ = 0;
    uint32_t output_last_report_ms_ = 0;
    bool output_first_nonzero_reported_ = false;

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);
    void ReportOutputTelemetry(bool force = false);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    // USB playback yields to native speech without opening any microphone.
    int WriteUsbPcm(const int16_t* data, int samples);
    void Start() override;
    CoreS3AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        uint8_t aw88298_addr, uint8_t es7210_addr, bool input_reference);
    virtual ~CoreS3AudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _BOX_AUDIO_CODEC_H

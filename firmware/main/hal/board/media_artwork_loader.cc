/* SPDX-License-Identifier: MIT */
#include "media_artwork_loader.h"

#include <board.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <string>
#include <utility>

#define TAG "MediaArtworkLoader"

namespace {
constexpr size_t kMaxArtworkBytes = 512 * 1024;

std::string UrlEncode(const std::string& value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[byte >> 4]);
            encoded.push_back(kHex[byte & 0x0F]);
        }
    }
    return encoded;
}
}

void MediaArtworkLoader::Fetch(uint32_t generation, const std::string& track_identity, void* target,
                               Callback callback, const std::shared_ptr<CallbackGate>& gate)
{
    auto* context = new FetchContext{generation, track_identity, target, callback, gate};
    if (xTaskCreate(&MediaArtworkLoader::FetchTask, "media_artwork", 6144, context, 3, nullptr) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create artwork fetch task");
        delete context;
    }
}

void MediaArtworkLoader::Close(const std::shared_ptr<CallbackGate>& gate)
{
    if (gate == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(gate->mutex);
    gate->closed = true;
}

void MediaArtworkLoader::FetchTask(void* arg)
{
    std::unique_ptr<FetchContext> context(static_cast<FetchContext*>(arg));
    std::unique_ptr<LvglImage> image;

    do {
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        if (http == nullptr) {
            ESP_LOGW(TAG, "Failed to create HTTP client");
            break;
        }
        std::string url = std::string(CONFIG_STACKCHAN_MEDIA_API_URL) + "/artwork";
        if (!context->track_identity.empty()) {
            url += "?identity=" + UrlEncode(context->track_identity);
        }
        if (!http->Open("GET", url)) {
            ESP_LOGW(TAG, "Failed to open %s", url.c_str());
            break;
        }
        const int status = http->GetStatusCode();
        if (status != 200) {
            if (status != 404) {
                ESP_LOGW(TAG, "Unexpected status %d", status);
            }
            http->Close();
            break;
        }
        const size_t content_length = http->GetBodyLength();
        if (content_length == 0 || content_length > kMaxArtworkBytes) {
            ESP_LOGW(TAG, "Invalid body length %u", (unsigned)content_length);
            http->Close();
            break;
        }
        uint8_t* data = static_cast<uint8_t*>(
            heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (data == nullptr) {
            data = static_cast<uint8_t*>(heap_caps_malloc(content_length, MALLOC_CAP_8BIT));
        }
        if (data == nullptr) {
            ESP_LOGW(TAG, "Failed to allocate %u bytes", (unsigned)content_length);
            http->Close();
            break;
        }
        size_t total_read = 0;
        while (total_read < content_length) {
            const int read = http->Read(reinterpret_cast<char*>(data) + total_read, content_length - total_read);
            if (read <= 0) {
                break;
            }
            total_read += read;
        }
        http->Close();
        if (total_read != content_length) {
            ESP_LOGW(TAG, "Incomplete body");
            heap_caps_free(data);
            break;
        }
        try {
            image = std::make_unique<LvglAllocatedImage>(data, total_read);
        } catch (const std::exception& error) {
            ESP_LOGW(TAG, "Failed to decode image: %s", error.what());
            heap_caps_free(data);
            break;
        }
        if (context->gate == nullptr) {
            ESP_LOGW(TAG, "Artwork fetch: callback gate is missing");
            break;
        }
        // Hold the gate while invoking the target callback. Close() therefore
        // cannot return until a callback already in progress has finished,
        // eliminating the target-after-destruction race.
        std::lock_guard<std::mutex> callback_lock(context->gate->mutex);
        if (context->gate->closed) {
            ESP_LOGI(TAG, "Artwork fetch: discarding result after screen close");
            break;
        }
        context->callback(context->target, context->generation, std::move(image));
    } while (false);

    vTaskDelete(nullptr);
}

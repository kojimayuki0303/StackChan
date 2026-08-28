/* SPDX-License-Identifier: MIT */
#include "media_artwork_loader.h"

#include <board.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <string>

#define TAG "MediaArtworkLoader"

namespace {
constexpr size_t kMaxArtworkBytes = 512 * 1024;
}

void MediaArtworkLoader::Fetch(uint32_t generation, void* target, Callback callback)
{
    auto* context = new FetchContext{generation, target, callback};
    if (xTaskCreate(&MediaArtworkLoader::FetchTask, "media_artwork", 6144, context, 3, nullptr) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create artwork fetch task");
        delete context;
    }
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
        const std::string url = std::string(CONFIG_STACKCHAN_MEDIA_API_URL) + "/artwork";
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
        context->callback(context->target, context->generation, std::move(image));
    } while (false);

    vTaskDelete(nullptr);
}

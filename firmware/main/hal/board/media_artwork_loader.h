/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <display/lvgl_display/lvgl_image.h>

class MediaArtworkLoader {
public:
    using Callback = void (*)(void* target, uint32_t generation, std::unique_ptr<LvglImage> image);

    // The screen owns this gate.  Artwork fetches may outlive an LVGL screen
    // while an HTTP request is in flight, so callbacks are serialized with
    // screen destruction instead of retaining a naked target pointer.
    struct CallbackGate {
        std::mutex mutex;
        bool closed = false;
    };

    static void Fetch(uint32_t generation, const std::string& track_identity, void* target,
                      Callback callback, const std::shared_ptr<CallbackGate>& gate);
    static void Close(const std::shared_ptr<CallbackGate>& gate);

private:
    struct FetchContext {
        uint32_t generation;
        std::string track_identity;
        void* target;
        Callback callback;
        std::shared_ptr<CallbackGate> gate;
    };

    static void FetchTask(void* arg);
};

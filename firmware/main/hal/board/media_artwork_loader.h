/* SPDX-License-Identifier: MIT */
#pragma once

#include <cstdint>
#include <memory>

#include <display/lvgl_display/lvgl_image.h>

class MediaArtworkLoader {
public:
    using Callback = void (*)(void* target, uint32_t generation, std::unique_ptr<LvglImage> image);

    static void Fetch(uint32_t generation, void* target, Callback callback);

private:
    struct FetchContext {
        uint32_t generation;
        void* target;
        Callback callback;
    };

    static void FetchTask(void* arg);
};

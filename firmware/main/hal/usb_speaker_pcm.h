#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace usb_speaker {

// USB PCM is explicitly little endian; do not assume alignment of endpoint data.
inline size_t StereoToMono(const uint8_t* src, size_t bytes, int16_t* dst,
                           size_t capacity, unsigned volume, bool muted)
{
    const size_t frames = std::min(bytes / 4, capacity);
    const int32_t gain = muted ? 0 : static_cast<int32_t>(std::min(volume, 100u));
    for (size_t i = 0; i < frames; ++i) {
        const auto left = static_cast<int16_t>(src[i * 4] | (src[i * 4 + 1] << 8));
        const auto right = static_cast<int16_t>(src[i * 4 + 2] | (src[i * 4 + 3] << 8));
        dst[i] = static_cast<int16_t>((static_cast<int32_t>(left) + right) * gain / 200);
    }
    return frames;
}

// Native speech/alerts retain priority; wrap-safe for the millisecond clock.
inline bool NativeHasPriority(uint32_t now, uint32_t last, bool seen)
{
    return seen && static_cast<uint32_t>(now - last) < 250;
}

} // namespace usb_speaker

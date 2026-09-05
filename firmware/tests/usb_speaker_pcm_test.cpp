/* SPDX-License-Identifier: MIT */
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "../main/hal/usb_speaker_pcm.h"

namespace {

void expect(bool actual, bool expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void expectEqual(std::int16_t actual, std::int16_t expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

void expectSize(std::size_t actual, std::size_t expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

std::vector<std::uint8_t> stereo(std::initializer_list<std::pair<std::int16_t, std::int16_t>> frames)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(frames.size() * 4);
    for (const auto [left, right] : frames) {
        const auto append = [&bytes](std::int16_t sample) {
            const auto raw = static_cast<std::uint16_t>(sample);
            bytes.push_back(static_cast<std::uint8_t>(raw));
            bytes.push_back(static_cast<std::uint8_t>(raw >> 8));
        };
        append(left);
        append(right);
    }
    return bytes;
}

void testSignedFullScaleDoesNotOverflow()
{
    const auto input = stereo({
        {std::numeric_limits<std::int16_t>::max(), std::numeric_limits<std::int16_t>::max()},
        {std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::min()},
    });
    std::int16_t output[2]{};
    expectSize(usb_speaker::StereoToMono(input.data(), input.size(), output, 2, 100, false), 2,
               "fullscale frame count");
    expectEqual(output[0], std::numeric_limits<std::int16_t>::max(), "positive fullscale");
    expectEqual(output[1], std::numeric_limits<std::int16_t>::min(), "negative fullscale");
}

void testAntiphaseCancels()
{
    const auto input = stereo({{std::numeric_limits<std::int16_t>::max(),
                                std::numeric_limits<std::int16_t>::min()}});
    std::int16_t output = 123;
    expectSize(usb_speaker::StereoToMono(input.data(), input.size(), &output, 1, 100, false), 1,
               "antiphase frame count");
    expectEqual(output, 0, "antiphase downmix");
}

void testGainMuteAndVolumeClamp()
{
    const auto input = stereo({{10000, 10000}});
    std::int16_t output = 0;
    usb_speaker::StereoToMono(input.data(), input.size(), &output, 1, 50, false);
    expectEqual(output, 5000, "half gain");
    usb_speaker::StereoToMono(input.data(), input.size(), &output, 1, 1000, false);
    expectEqual(output, 10000, "volume clamp");
    usb_speaker::StereoToMono(input.data(), input.size(), &output, 1, 100, true);
    expectEqual(output, 0, "mute");
}

void testInputAndOutputBounds()
{
    const auto input = stereo({{1, 1}, {2, 2}});
    const std::int16_t canary = -3210;
    std::int16_t output[2] = {canary, canary};
    expectSize(usb_speaker::StereoToMono(input.data(), input.size(), output, 1, 100, false), 1,
               "capacity bound frame count");
    expectEqual(output[0], 1, "capacity bound first frame");
    expectEqual(output[1], canary, "capacity bound canary");

    output[0] = canary;
    expectSize(usb_speaker::StereoToMono(input.data(), 3, output, 1, 100, false), 0,
               "partial frame count");
    expectEqual(output[0], canary, "partial frame does not write");
}

void testNativePriorityIsWrapSafeAndBounded()
{
    expect(usb_speaker::NativeHasPriority(1000, 751, true), true, "priority before timeout");
    expect(usb_speaker::NativeHasPriority(1001, 751, true), false, "priority timeout");
    expect(usb_speaker::NativeHasPriority(0x00000010u, 0xfffffff0u, true), true,
           "priority across clock wrap");
    expect(usb_speaker::NativeHasPriority(0x00000100u, 0xfffffff0u, true), false,
           "priority elapsed across clock wrap");
    expect(usb_speaker::NativeHasPriority(1000, 751, false), false, "unseen native audio");
}

}  // namespace

int main()
{
    testSignedFullScaleDoesNotOverflow();
    testAntiphaseCancels();
    testGainMuteAndVolumeClamp();
    testInputAndOutputBounds();
    testNativePriorityIsWrapSafeAndBounded();
    return 0;
}

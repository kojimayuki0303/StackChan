#pragma once
#include <cstddef>
#include <cstdint>

void RecordUsbSpeakerWrite(const int16_t* pcm, size_t frames, int written);

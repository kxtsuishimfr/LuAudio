#pragma once

#include <cstdint>

namespace LuAudio::Audio {

enum class SampleType {
    Float32
};

enum class ChannelLayout {
    Interleaved
};

struct AudioFormat {
    std::uint32_t sampleRate = 48000;
    std::uint32_t channelCount = 2;
    SampleType sampleType = SampleType::Float32;
    ChannelLayout channelLayout = ChannelLayout::Interleaved;

    bool IsValid() const noexcept;
};

}

#pragma once

#include <cstdint>

namespace LuAudio::Audio {

/**
 * @summary Describes the sample data type.
 */
enum class SampleType {
    Float32
};

/**
 * @summary Describes how channel samples are stored.
 */
enum class ChannelLayout {
    Interleaved
};

/**
 * @summary Describes the format of audio samples.
 */
struct AudioFormat {
    /** @summary Samples per second. */
    std::uint32_t sampleRate = 48000;
    /** @summary Number of channels. */
    std::uint32_t channelCount = 2;
    /** @summary Sample value type. */
    SampleType sampleType = SampleType::Float32;
    /** @summary Channel storage layout. */
    ChannelLayout channelLayout = ChannelLayout::Interleaved;

    /**
     * @summary Checks whether the format has usable rate and channel values.
     * @returns True when the format is valid.
     */
    bool IsValid() const noexcept;
};

}

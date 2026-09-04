#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Audio/Contracts/IAudioDecoder.h>

namespace LuAudio::Audio {

/**
 * @summary Creates the default audio backend for the current platform.
 * @returns A closed backend ready to be opened.
 */
std::unique_ptr<IAudioBackend> CreateBackend();

/**
 * @summary Creates the default audio decoder for the current platform.
 * @returns A decoder ready to open an audio file.
 */
std::unique_ptr<IAudioDecoder> CreateDecoder();

}

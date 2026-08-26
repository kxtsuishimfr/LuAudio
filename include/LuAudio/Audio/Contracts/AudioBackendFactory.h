#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Contracts/IAudioBackend.h>

namespace LuAudio::Audio {

/**
 * @summary Creates the default audio backend for the current platform.
 * @returns A closed backend ready to be opened.
 */
std::unique_ptr<IAudioBackend> CreateBackend();

}

#pragma once

#include <LuAudio/Plugins/Contracts/PluginAbi.h>

namespace LuAudio::Plugins::SDK {

/** @summary Provides the author-facing C++ plugin lifecycle and DSP interface. */
class PluginBase {
public:
    virtual ~PluginBase() = default;

    /** @summary Initializes the plugin instance and returns whether creation may continue. */
    virtual bool Init(const PluginInstanceConfig&) { return true; }

    /** @summary Processes one audio buffer in place on the audio thread. */
    virtual bool Process(PluginAudioBuffer& buffer) = 0;

    /** @summary Resets plugin state. */
    virtual void Reset() {}

    /** @summary Sets a named plugin parameter. */
    virtual bool SetParameter(const char*, float) { return false; }

    /** @summary Reads a named plugin parameter. */
    virtual bool GetParameter(const char*, float&) { return false; }
};

}
#pragma once

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Playback/AudioMixer.h>
#include <LuAudio/Audio/Playback/AudioPlayer.h>
#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/AudioStreamConfig.h>
#include <LuAudio/Audio/Contracts/IAudioDecoder.h>
#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Audio/Contracts/IAudioEffect.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Audio/Sources/AudioFile.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>
#include <LuAudio/Audio/Sources/Mp3FileReader.h>
#include <LuAudio/Audio/Sources/WavFileReader.h>
#include <LuAudio/Plugins/PluginAbi.h>
#include <LuAudio/Plugins/PluginEffectAdapter.h>
#include <LuAudio/Plugins/PluginHandle.h>
#include <LuAudio/Plugins/PluginManager.h>
#include <LuAudio/Providers/Contracts/IPluginLibrary.h>
#include <LuAudio/Providers/Contracts/IPluginProvider.h>
#include <LuAudio/Utils/Diagnostics/Log.h>

#ifdef _WIN32
#include <LuAudio/Providers/Windows/WAudioDecoder.h>
#include <LuAudio/Providers/Windows/WPluginProvider.h>
#include <LuAudio/Providers/Windows/Wasapi/WasapiBackend.h>
#include <LuAudio/Providers/Windows/Wasapi/WasapiDevice.h>
#endif

#ifdef __ANDROID__
#include <LuAudio/Providers/Android/AAudioDecoder.h>
#include <LuAudio/Providers/Android/APluginProvider.h>
#include <LuAudio/Providers/Android/Oboe/OboeBackend.h>
#endif

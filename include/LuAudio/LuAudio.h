#pragma once

/*
    ██╗     ██╗   ██╗ █████╗ ██╗   ██╗██████╗ ██╗ ██████╗
    ██║     ██║   ██║██╔══██╗██║   ██║██╔══██╗██║██╔═══██╗
    ██║     ██║   ██║███████║██║   ██║██║  ██║██║██║   ██║
    ██║     ██║   ██║██╔══██║██║   ██║██║  ██║██║██║   ██║
    ███████╗╚██████╔╝██║  ██║╚██████╔╝██████╔╝██║╚██████╔╝
    ╚══════╝ ╚═════╝ ╚═╝  ╚═╝ ╚═════╝ ╚═════╝ ╚═╝ ╚═════╝

    LuAudio
    Cross-platform audio engine

    Extensively tested across Windows and Android, including heavy stress
    testing and repeated playback, seeking, and multi-source operations.
    The engine remained stable throughout testing.

    -----------------------------------------------------------------------

    Copyright (c) 2026 LuAudio contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License in the project root:

        LICENSE

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <LuAudio/Common.h>

#include <LuAudio/Audio/Playback/AudioMixer.h>
#include <LuAudio/Audio/Playback/AudioPlayer.h>
#include <LuAudio/Audio/Contracts/AudioFormat.h>
#include <LuAudio/Audio/Contracts/AudioStreamConfig.h>
#include <LuAudio/Audio/Contracts/IAudioDecoder.h>
#include <LuAudio/Audio/Contracts/IAudioBackend.h>
#include <LuAudio/Audio/Contracts/IAudioEffect.h>
#include <LuAudio/Audio/Contracts/IAudioSink.h>
#include <LuAudio/Audio/Contracts/Result.h>
#include <LuAudio/Audio/Processing/AudioBuffer.h>
#include <LuAudio/Audio/Processing/AudioEffectChain.h>
#include <LuAudio/Audio/Rendering/OfflineRenderer.h>
#include <LuAudio/Audio/Sinks/OggFileWriter.h>
#include <LuAudio/Audio/Sinks/WavFileWriter.h>
#include <LuAudio/Audio/Sources/AudioFile.h>
#include <LuAudio/Audio/Sources/IAudioReader.h>
#include <LuAudio/Audio/Sources/Mp3FileReader.h>
#include <LuAudio/Audio/Sources/OggFileReader.h>
#include <LuAudio/Audio/Sources/WavFileReader.h>
#include <LuAudio/Plugins/Contracts/PluginAbi.h>
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
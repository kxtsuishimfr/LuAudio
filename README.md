## LuAudio

## What is it?

LuAudio is an audio engine written in C++, it's cross-platform and built with performance in mind.

## What can it do?

LuAudio can playback audio files in many formats in real time, render them offline to a file.
It also has support for chaining effects, plugins, and much more things

## Is it stable?

LuAudio has been put through many stress-tests across all supported platforms to ensure stable usage.
Though, if you find any issues with it feel free to open an issue :D

## Where is documentation at?

Currently, LuAudio does not have a website for documentation. Though, you can read the header files for it, all the functions
are neatly documented for anyone who wants to read them.

## Building

LuAudio uses CMake for building.

### Windows

You'll need:

* Visual Studio with C++ tools
* Windows SDK
* CMake
* Ninja

Open LuAudio in Visual Studio and select one of the CMake presets from the configuration dropdown.

Available presets:

* `x64-debug`
* `x64-release`
* `x86-debug`
* `x86-release`

Then just build it through Visual Studio.

### Android

You'll need:

* Android NDK 29.0.14206865
* CMake
* Ninja

Select one of the Android presets:

* `android-arm64-debug`
* `android-arm64-release`

Then build it normally.

### Hardcoded paths

Some paths in `CMakePresets.json` are hardcoded to my setup, so you'll probably need to change them.

Android NDK:

```
E:/Android/Sdk/ndk/29.0.14206865/build/cmake/android.toolchain.cmake
E:/Android/Sdk/ndk/29.0.14206865
```

If your NDK is somewhere else, change `CMAKE_TOOLCHAIN_FILE` and `CMAKE_ANDROID_NDK`.

Windows SDK:

```
C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe
C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe
```

If you have a different Windows SDK version, change `CMAKE_RC_COMPILER` and `CMAKE_MT`.

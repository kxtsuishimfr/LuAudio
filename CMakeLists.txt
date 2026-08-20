cmake_minimum_required(VERSION 3.22.1)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("LuAudio")

include(CTest)

option(LUAUDIO_ANDROID_TESTS "Build the Android test runner" OFF)

set(LUAUDIO_SOURCES
    "src/Audio/Contracts/AudioFormat.cpp"
    "src/Audio/Contracts/AudioStreamConfig.cpp"
    "src/Audio/Contracts/IAudioBackend.cpp"
    "src/Audio/Contracts/Result.cpp"
    "src/Audio/Processing/AudioBuffer.cpp"
    "src/Audio/Sources/AudioFile.cpp"
    "src/Audio/Sources/WavFileReader.cpp"
    "src/Utils/Diagnostics/Log.cpp"
    "include/LuAudio/Audio/Contracts/AudioFormat.h"
    "include/LuAudio/Audio/Contracts/AudioStreamConfig.h"
    "include/LuAudio/Audio/Contracts/IAudioBackend.h"
    "include/LuAudio/Audio/Contracts/Result.h"
    "include/LuAudio/Audio/Processing/AudioBuffer.h"
    "include/LuAudio/Audio/Sources/AudioFile.h"
    "include/LuAudio/Audio/Sources/WavFileReader.h"
    "include/LuAudio/Common.h"
    "include/LuAudio/LuAudio.h"
    "include/LuAudio/Utils/Diagnostics/Log.h"
)

if (WIN32)
    list(APPEND LUAUDIO_SOURCES
        "src/Providers/Windows/Wasapi/WasapiBackend.cpp"
        "src/Providers/Windows/Wasapi/WasapiDevice.cpp"
        "include/LuAudio/Providers/Windows/Wasapi/WasapiBackend.h"
        "include/LuAudio/Providers/Windows/Wasapi/WasapiDevice.h"
    )
endif()

add_library(LuAudio SHARED ${LUAUDIO_SOURCES})

set_target_properties(LuAudio PROPERTIES
    OUTPUT_NAME "LuAudio"
)

if (WIN32)
    set_target_properties(LuAudio PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_include_directories(LuAudio PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if (WIN32)
    target_link_libraries(LuAudio PRIVATE ole32 avrt)
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET LuAudio PROPERTY CXX_STANDARD 20)
endif()

if (ANDROID)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/Oboe" "${CMAKE_CURRENT_BINARY_DIR}/oboe-build")

    add_library(LuAudioOboe SHARED
        "src/Providers/Android/Oboe/OboeBackend.cpp"
        "include/LuAudio/Providers/Android/Oboe/OboeBackend.h"
    )

    target_include_directories(LuAudioOboe PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/Oboe/include"
    )
    target_link_libraries(LuAudioOboe PRIVATE LuAudio oboe)
    target_compile_features(LuAudioOboe PRIVATE cxx_std_20)

    if (LUAUDIO_ANDROID_TESTS)
        add_library(native-lib SHARED
            "tests/Android/app/src/main/cpp/AndroidTestMain.cpp"
        )
        target_include_directories(native-lib PRIVATE
            "${PROJECT_SOURCE_DIR}/include"
        )
        target_link_libraries(native-lib PRIVATE LuAudioOboe LuAudio log android)
        target_compile_features(native-lib PRIVATE cxx_std_20)
    endif()
endif()

if (BUILD_TESTING AND WIN32)
    include(FetchContent)

    FetchContent_Declare(
        GoogleTest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(GoogleTest)

    add_executable(LuAudioProviderTests
        "tests/Windows/Wasapi/WasapiBackendTests.cpp"
        "tests/Windows/Wasapi/WasapiDeviceTests.cpp"
    )
    target_link_libraries(LuAudioProviderTests PRIVATE LuAudio GTest::gtest_main)
    target_compile_features(LuAudioProviderTests PRIVATE cxx_std_20)

    include(GoogleTest)
    gtest_discover_tests(LuAudioProviderTests)

    add_executable(LuAudioWasapiSilenceTest
        "tests/Windows/Wasapi/WasapiSilenceTest.cpp"
    )
    target_link_libraries(LuAudioWasapiSilenceTest PRIVATE LuAudio)
    target_compile_features(LuAudioWasapiSilenceTest PRIVATE cxx_std_20)

    add_executable(LuAudioWasapiFilePlaybackTest
        "tests/Windows/Wasapi/WasapiFilePlaybackTest.cpp"
    )
    target_link_libraries(LuAudioWasapiFilePlaybackTest PRIVATE LuAudio)
    target_compile_features(LuAudioWasapiFilePlaybackTest PRIVATE cxx_std_20)
endif()
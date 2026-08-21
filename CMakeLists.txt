cmake_minimum_required(VERSION 3.22.1)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("LuAudio")

include(CTest)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuAudioPlugin.cmake")

option(LUAUDIO_ANDROID_TESTS "Build the Android test runner" OFF)
set(LUAUDIO_ANDROID_TEST_SOURCE
    "${PROJECT_SOURCE_DIR}/tests/Android/app/src/main/cpp/AndroidTestMain.cpp"
    CACHE FILEPATH "Android test runner native source")

add_library(LuAudioPluginContracts INTERFACE)
target_include_directories(LuAudioPluginContracts INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(LuAudioPluginContracts PROPERTIES
    EXPORT_NAME PluginContracts)

add_library(LuAudioPluginSDK INTERFACE)
target_link_libraries(LuAudioPluginSDK INTERFACE LuAudioPluginContracts)
target_include_directories(LuAudioPluginSDK INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(LuAudioPluginSDK PROPERTIES
    EXPORT_NAME PluginSDK)

add_library(LuAudio::PluginContracts ALIAS LuAudioPluginContracts)
add_library(LuAudio::PluginSDK ALIAS LuAudioPluginSDK)

set(LUAUDIO_CORE_SOURCES
    "src/Audio/Contracts/AudioFormat.cpp"
    "src/Audio/Contracts/AudioStreamConfig.cpp"
    "src/Audio/Contracts/IAudioBackend.cpp"
    "src/Audio/Contracts/Result.cpp"
    "src/Audio/Rendering/OfflineRenderer.cpp"
    "src/Audio/Playback/AudioMixer.cpp"
    "src/Audio/Playback/AudioMixerRender.cpp"
    "src/Audio/Playback/AudioPlayer.cpp"
    "src/Audio/Processing/AudioBuffer.cpp"
    "src/Audio/Processing/AudioEffectChain.cpp"
    "src/Audio/Sinks/WavFileWriter.cpp"
    "src/Audio/Sources/AudioFile.cpp"
    "src/Audio/Sources/Mp3FileReader.cpp"
    "src/Audio/Sources/WavFileReader.cpp"
    "src/Utils/Diagnostics/Log.cpp"
    "include/LuAudio/Audio/Contracts/AudioFormat.h"
    "include/LuAudio/Audio/Contracts/AudioStreamConfig.h"
    "include/LuAudio/Audio/Contracts/IAudioBackend.h"
    "include/LuAudio/Audio/Contracts/IAudioSink.h"
    "include/LuAudio/Audio/Contracts/Result.h"
    "include/LuAudio/Audio/Playback/AudioMixer.h"
    "include/LuAudio/Audio/Playback/AudioPlayer.h"
    "include/LuAudio/Audio/Rendering/OfflineRenderer.h"
    "include/LuAudio/Audio/Processing/AudioBuffer.h"
    "include/LuAudio/Audio/Processing/AudioEffectChain.h"
    "include/LuAudio/Audio/Sinks/WavFileWriter.h"
    "include/LuAudio/Audio/Contracts/IAudioEffect.h"
    "include/LuAudio/Audio/Sources/AudioFile.h"
    "include/LuAudio/Audio/Sources/Mp3FileReader.h"
    "include/LuAudio/Audio/Sources/WavFileReader.h"
    "include/LuAudio/Audio/Sources/IAudioReader.h"
    "include/LuAudio/Audio/Contracts/IAudioDecoder.h"
    "include/LuAudio/Common.h"
    "include/LuAudio/LuAudio.h"
    "include/LuAudio/Utils/Diagnostics/Log.h"
)

add_library(LuAudioCore SHARED ${LUAUDIO_CORE_SOURCES})

set_target_properties(LuAudioCore PROPERTIES
    OUTPUT_NAME "AudioCore"
)

if (WIN32)
    set_target_properties(LuAudioCore PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_include_directories(LuAudioCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET LuAudioCore PROPERTY CXX_STANDARD 20)
endif()

add_library(LuAudioPluginHost SHARED
    "src/Plugins/PluginEffectAdapter.cpp"
    "src/Plugins/PluginHandle.cpp"
    "src/Plugins/PluginManager.cpp"
    "include/LuAudio/Plugins/PluginEffectAdapter.h"
    "include/LuAudio/Plugins/PluginHandle.h"
    "include/LuAudio/Plugins/PluginManager.h"
)

target_include_directories(LuAudioPluginHost PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(LuAudioPluginHost PUBLIC LuAudioPluginContracts)
target_link_libraries(LuAudioPluginHost PRIVATE LuAudioCore)
target_compile_features(LuAudioPluginHost PRIVATE cxx_std_20)

if (WIN32)
    set_target_properties(LuAudioPluginHost PROPERTIES
        OUTPUT_NAME "PluginHost"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

if (WIN32)
    add_library(LuAudioWindowsProvider SHARED
        "src/Providers/Windows/WAudioDecoder.cpp"
        "src/Providers/Windows/WPluginProvider.cpp"
        "src/Providers/Windows/Wasapi/WasapiBackend.cpp"
        "src/Providers/Windows/Wasapi/WasapiDevice.cpp"
        "include/LuAudio/Providers/Windows/WAudioDecoder.h"
        "include/LuAudio/Providers/Windows/WPluginProvider.h"
        "include/LuAudio/Providers/Windows/Wasapi/WasapiBackend.h"
        "include/LuAudio/Providers/Windows/Wasapi/WasapiDevice.h"
    )
    target_include_directories(LuAudioWindowsProvider PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(LuAudioWindowsProvider PRIVATE
        LuAudioCore ole32 avrt mfplat mfreadwrite mfuuid
    )
    target_compile_features(LuAudioWindowsProvider PRIVATE cxx_std_20)
    set_target_properties(LuAudioWindowsProvider PROPERTIES
        OUTPUT_NAME "WPluginProvider"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )

    add_library(LuAudio INTERFACE)
    target_link_libraries(LuAudio INTERFACE LuAudioCore LuAudioWindowsProvider)
    add_library(LuAudio::Core ALIAS LuAudioCore)
    add_library(LuAudio::PluginHost ALIAS LuAudioPluginHost)
    add_library(LuAudio::WindowsProvider ALIAS LuAudioWindowsProvider)
endif()

if (ANDROID)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/Oboe" "${CMAKE_CURRENT_BINARY_DIR}/oboe-build")

    add_library(LuAudioHallReverbPlugin SHARED
        "tests/Windows/Plugins/HallReverbPlugin.cpp"
    )
    target_include_directories(LuAudioHallReverbPlugin PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/sdk/include"
    )
    target_compile_features(LuAudioHallReverbPlugin PRIVATE cxx_std_20)
    set_target_properties(LuAudioHallReverbPlugin PROPERTIES
        OUTPUT_NAME "ProfessionalHallReverb"
    )

    add_library(LuAudioAndroidProvider SHARED
        "src/Providers/Android/AAudioDecoder.cpp"
        "src/Providers/Android/APluginProvider.cpp"
        "src/Providers/Android/Oboe/OboeBackend.cpp"
        "include/LuAudio/Providers/Android/AAudioDecoder.h"
        "include/LuAudio/Providers/Android/APluginProvider.h"
        "include/LuAudio/Providers/Android/Oboe/OboeBackend.h"
    )

    target_include_directories(LuAudioAndroidProvider PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_include_directories(LuAudioAndroidProvider PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/Oboe/include"
    )
    target_link_libraries(LuAudioAndroidProvider PRIVATE LuAudioCore oboe mediandk)
    target_compile_features(LuAudioAndroidProvider PRIVATE cxx_std_20)

    add_library(LuAudioOboe ALIAS LuAudioAndroidProvider)
    add_library(LuAudio INTERFACE)
    target_link_libraries(LuAudio INTERFACE LuAudioCore LuAudioAndroidProvider)
    add_library(LuAudio::Core ALIAS LuAudioCore)
    add_library(LuAudio::PluginHost ALIAS LuAudioPluginHost)
    add_library(LuAudio::AndroidProvider ALIAS LuAudioAndroidProvider)

    add_library(LuAudioBitcrusherPlugin SHARED
        "tests/Windows/Plugins/BitcrusherPlugin.cpp"
    )
    target_include_directories(LuAudioBitcrusherPlugin PRIVATE
        "${PROJECT_SOURCE_DIR}/sdk/include"
    )
    target_compile_features(LuAudioBitcrusherPlugin PRIVATE cxx_std_20)
    set_target_properties(LuAudioBitcrusherPlugin PROPERTIES
        OUTPUT_NAME "Bitcrusher")

    if (LUAUDIO_ANDROID_TESTS)
        add_library(native-lib SHARED
            "${LUAUDIO_ANDROID_TEST_SOURCE}"
        )
        target_include_directories(native-lib PRIVATE
            "${PROJECT_SOURCE_DIR}/include"
        )
        target_link_libraries(native-lib PRIVATE
            LuAudioCore
            LuAudioPluginHost
            LuAudioAndroidProvider
            LuAudioBitcrusherPlugin
            log
            android
        )
        add_dependencies(native-lib LuAudioBitcrusherPlugin)
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

    add_library(LuAudioTestPlugin SHARED
        "tests/Windows/Plugins/TestPlugin.cpp"
    )
    target_include_directories(LuAudioTestPlugin PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(LuAudioTestPlugin PRIVATE cxx_std_20)

    add_library(LuAudioHallReverbPlugin SHARED
        "tests/Windows/Plugins/HallReverbPlugin.cpp"
    )
    target_include_directories(LuAudioHallReverbPlugin PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_compile_features(LuAudioHallReverbPlugin PRIVATE cxx_std_20)
    set_target_properties(LuAudioHallReverbPlugin PROPERTIES
        OUTPUT_NAME "ProfessionalHallReverb"
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )

    add_library(LuAudioBitcrusherPlugin MODULE
        "tests/Windows/Plugins/BitcrusherPlugin.cpp"
    )
    target_link_libraries(LuAudioBitcrusherPlugin PRIVATE LuAudioPluginSDK)
    target_compile_features(LuAudioBitcrusherPlugin PRIVATE cxx_std_20)
    set_target_properties(LuAudioBitcrusherPlugin PROPERTIES
        OUTPUT_NAME "Bitcrusher"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)

    add_executable(LuAudioPluginTests
        "tests/Windows/Plugins/AudioEffectChainTests.cpp"
        "tests/Windows/Plugins/PluginManagerTests.cpp"
    )
    target_link_libraries(LuAudioPluginTests PRIVATE
        LuAudioCore
        LuAudioPluginHost
        LuAudioWindowsProvider
        GTest::gtest_main
    )
    target_compile_features(LuAudioPluginTests PRIVATE cxx_std_20)
    target_compile_definitions(LuAudioPluginTests PRIVATE
        LUAUDIO_TEST_PLUGIN_PATH="$<TARGET_FILE:LuAudioTestPlugin>"
    )
    add_dependencies(LuAudioPluginTests LuAudioTestPlugin)

    add_executable(LuAudioPluginSdkTests
        "tests/Plugins/PluginTrampolineTests.cpp"
    )
    target_link_libraries(LuAudioPluginSdkTests PRIVATE
        LuAudioPluginSDK
        GTest::gtest_main
    )
    target_compile_features(LuAudioPluginSdkTests PRIVATE cxx_std_20)

    add_executable(LuAudioMixerTests
        "tests/Windows/AudioMixerTests.cpp"
    )
    target_link_libraries(LuAudioMixerTests PRIVATE LuAudioCore GTest::gtest_main)
    target_compile_features(LuAudioMixerTests PRIVATE cxx_std_20)

    add_executable(LuAudioOfflineRenderingTests
        "tests/Windows/OfflineRenderingTests.cpp"
    )
    target_link_libraries(LuAudioOfflineRenderingTests PRIVATE LuAudioCore GTest::gtest_main)
    target_compile_features(LuAudioOfflineRenderingTests PRIVATE cxx_std_20)

    add_executable(LuAudioProviderTests
        "tests/Windows/Wasapi/WasapiBackendTests.cpp"
        "tests/Windows/Wasapi/WasapiDeviceTests.cpp"
    )
    target_link_libraries(LuAudioProviderTests PRIVATE LuAudio GTest::gtest_main)
    target_compile_features(LuAudioProviderTests PRIVATE cxx_std_20)

    include(GoogleTest)
    gtest_discover_tests(LuAudioProviderTests)
    gtest_discover_tests(LuAudioPluginTests)
    gtest_discover_tests(LuAudioPluginSdkTests)
    gtest_discover_tests(LuAudioMixerTests)
    gtest_discover_tests(LuAudioOfflineRenderingTests)

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

    add_executable(LuAudioExportAudioTest
        "tests/Windows/Wasapi/ExportAudioTest.cpp"
    )
    target_link_libraries(LuAudioExportAudioTest PRIVATE LuAudio LuAudioPluginHost)
    target_compile_features(LuAudioExportAudioTest PRIVATE cxx_std_20)
    target_compile_definitions(LuAudioExportAudioTest PRIVATE
        LUAUDIO_HALL_REVERB_PLUGIN_PATH="$<TARGET_FILE:LuAudioHallReverbPlugin>"
    )
    add_dependencies(LuAudioExportAudioTest LuAudioHallReverbPlugin)

    add_executable(LuAudioWasapiSeekPlaybackTest
        "tests/Windows/Wasapi/WasapiSeekPlaybackTest.cpp"
    )
    target_link_libraries(LuAudioWasapiSeekPlaybackTest PRIVATE LuAudio)
    target_compile_features(LuAudioWasapiSeekPlaybackTest PRIVATE cxx_std_20)

    add_executable(LuAudioWasapiMixerPlaybackTest
        "tests/Windows/Wasapi/WasapiMixerPlaybackTest.cpp"
    )
    target_link_libraries(LuAudioWasapiMixerPlaybackTest PRIVATE LuAudio)
    target_compile_features(LuAudioWasapiMixerPlaybackTest PRIVATE cxx_std_20)

    add_executable(LuAudioReverbSeekTest
        "tests/Windows/Plugins/ReverbSeekPlaybackTest.cpp"
    )
    target_link_libraries(LuAudioReverbSeekTest PRIVATE LuAudio)
    target_compile_features(LuAudioReverbSeekTest PRIVATE cxx_std_20)

    add_executable(LuAudioBitcrusherPlaybackTest
        "tests/Windows/Plugins/BitcrusherPlaybackTest.cpp"
    )
    target_link_libraries(LuAudioBitcrusherPlaybackTest PRIVATE
        LuAudio
        LuAudioPluginHost
    )
    target_compile_features(LuAudioBitcrusherPlaybackTest PRIVATE cxx_std_20)
    target_compile_definitions(LuAudioBitcrusherPlaybackTest PRIVATE
        LUAUDIO_BITCRUSHER_PLUGIN_PATH="$<TARGET_FILE:LuAudioBitcrusherPlugin>"
    )
    add_dependencies(LuAudioBitcrusherPlaybackTest LuAudioBitcrusherPlugin)
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuAudioPlugin.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LuAudioPluginSDK")
install(TARGETS LuAudioPluginContracts LuAudioPluginSDK
    EXPORT LuAudioPluginSDKTargets)
install(EXPORT LuAudioPluginSDKTargets
    FILE LuAudioPluginSDKTargets.cmake
    NAMESPACE LuAudio::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LuAudioPluginSDK")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuAudioPluginSDKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LuAudioPluginSDKConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LuAudioPluginSDK")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuAudioPluginSDKConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LuAudioPluginSDK")
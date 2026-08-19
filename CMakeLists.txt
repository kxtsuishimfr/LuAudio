cmake_minimum_required (VERSION 3.10)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("LuAudio")

add_library(LuAudio SHARED
    "src/Audio/Contracts/AudioFormat.cpp"
    "src/Audio/Contracts/AudioStreamConfig.cpp"
    "src/Audio/Contracts/IAudioBackend.cpp"
    "src/Audio/Contracts/Result.cpp"
    "src/Audio/Processing/AudioBuffer.cpp"
    "src/Utils/Diagnostics/Log.cpp"
    "src/Providers/Windows/Wasapi/WasapiBackend.cpp"
    "src/Providers/Windows/Wasapi/WasapiDevice.cpp"
    "include/LuAudio/Audio/Contracts/AudioFormat.h"
    "include/LuAudio/Audio/Contracts/AudioStreamConfig.h"
    "include/LuAudio/Audio/Contracts/IAudioBackend.h"
    "include/LuAudio/Audio/Contracts/Result.h"
    "include/LuAudio/Audio/Processing/AudioBuffer.h"
    "include/LuAudio/Common.h"
    "include/LuAudio/Providers/Windows/Wasapi/WasapiBackend.h"
    "include/LuAudio/Providers/Windows/Wasapi/WasapiDevice.h"
    "include/LuAudio/Utils/Diagnostics/Log.h"
)

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
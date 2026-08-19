#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Utils {

enum class LogLevel {
    Info,
    Debug,
    Trace
};

class Log {
public:
    static void SetMinimumLevel(LogLevel level) noexcept;
    static LogLevel MinimumLevel() noexcept;

    static void Info(std::string_view message) noexcept;
    static void Debug(std::string_view message) noexcept;
    static void Trace(std::string_view message) noexcept;

private:
    static void Write(LogLevel level, std::string_view message) noexcept;
};

}

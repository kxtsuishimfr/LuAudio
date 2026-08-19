#include <cstdio>
#include <mutex>

#include <LuAudio/Utils/Diagnostics/Log.h>

namespace LuAudio::Utils {

namespace {

LogLevel minimumLevel = LogLevel::Info;
std::mutex logMutex;

const char* LevelName(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Trace:
        return "TRACE";
    }

    return "UNKNOWN";
}

bool IsEnabled(LogLevel level) noexcept
{
    return static_cast<int>(level) <= static_cast<int>(minimumLevel);
}

}

void Log::SetMinimumLevel(LogLevel level) noexcept
{
    std::lock_guard<std::mutex> lock(logMutex);
    minimumLevel = level;
}

LogLevel Log::MinimumLevel() noexcept
{
    std::lock_guard<std::mutex> lock(logMutex);
    return minimumLevel;
}

void Log::Info(std::string_view message) noexcept
{
    Write(LogLevel::Info, message);
}

void Log::Debug(std::string_view message) noexcept
{
    Write(LogLevel::Debug, message);
}

void Log::Trace(std::string_view message) noexcept
{
    Write(LogLevel::Trace, message);
}

void Log::Write(LogLevel level, std::string_view message) noexcept
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (!IsEnabled(level)) {
        return;
    }

    std::fprintf(stderr, "[LuAudio][%s] %.*s\n", LevelName(level), static_cast<int>(message.size()), message.data());
}

}

#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Utils {

/**
 * @summary Sets how much diagnostic output is written.
 */
enum class LogLevel {
    Info,
    Debug,
    Trace
};

/**
 * @summary Writes LuAudio diagnostic messages.
 */
class Log {
public:
    /**
     * @summary Sets the lowest enabled log level.
     * @param level Minimum level to write.
     */
    static void SetMinimumLevel(LogLevel level) noexcept;
    /**
     * @summary Gets the current minimum log level.
     * @returns Current minimum level.
     */
    static LogLevel MinimumLevel() noexcept;

    /**
     * @summary Writes an informational message.
     * @param message Message text.
     */
    static void Info(std::string_view message) noexcept;
    /**
     * @summary Writes a debug message.
     * @param message Message text.
     */
    static void Debug(std::string_view message) noexcept;
    /**
     * @summary Writes a trace message.
     * @param message Message text.
     */
    static void Trace(std::string_view message) noexcept;

private:
    /**
     * @summary Writes a message at the given level.
     * @internal
     */
    static void Write(LogLevel level, std::string_view message) noexcept;
};

}

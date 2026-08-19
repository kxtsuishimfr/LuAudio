#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Audio {

enum class ResultCode {
    Success,
    InvalidArgument,
    InvalidState,
    BackendUnavailable,
    ProcessingFailed
};

class Result {
public:
    static Result Success();
    static Result Failure(ResultCode code, std::string message);

    bool Succeeded() const noexcept;
    ResultCode Code() const noexcept;
    const std::string& Message() const noexcept;

private:
    Result(ResultCode code, std::string message);

    ResultCode code_;
    std::string message_;
};

}

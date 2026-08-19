#include <utility>

#include <LuAudio/Audio/Contracts/Result.h>

namespace LuAudio::Audio {

Result Result::Success()
{
    return Result(ResultCode::Success, {});
}

Result Result::Failure(ResultCode code, std::string message)
{
    return Result(code, std::move(message));
}

bool Result::Succeeded() const noexcept
{
    return code_ == ResultCode::Success;
}

ResultCode Result::Code() const noexcept
{
    return code_;
}

const std::string& Result::Message() const noexcept
{
    return message_;
}

Result::Result(ResultCode code, std::string message)
    : code_(code), message_(std::move(message))
{
}

}

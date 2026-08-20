#pragma once

#include <LuAudio/Common.h>

namespace LuAudio::Audio {

/**
 * @summary Describes the result category returned by an operation.
 */
enum class ResultCode {
    Success,
    InvalidArgument,
    InvalidState,
    BackendUnavailable,
    ProcessingFailed
};

/**
 * @summary Carries success or failure information.
 */
class Result {
public:
    /**
     * @summary Creates a successful result.
     * @returns A successful result.
     */
    static Result Success();
    /**
     * @summary Creates a failed result.
     * @param code Failure category.
     * @param message Short failure description.
     * @returns A failed result.
     */
    static Result Failure(ResultCode code, std::string message);

    /**
     * @summary Checks whether the operation succeeded.
     * @returns True for a successful result.
     */
    bool Succeeded() const noexcept;
    /**
     * @summary Gets the result category.
     * @returns The result code.
     */
    ResultCode Code() const noexcept;
    /**
     * @summary Gets the failure description.
     * @returns The result message.
     */
    const std::string& Message() const noexcept;

private:
    /**
     * @summary Creates a result with a code and message.
     * @internal
     */
    Result(ResultCode code, std::string message);

    ResultCode code_;
    std::string message_;
};

}

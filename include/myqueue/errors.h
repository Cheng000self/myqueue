/**
 * @file errors.h
 * @brief Error codes and exception classes for myqueue
 * 
 * Defines error handling infrastructure including error codes and
 * a custom exception class for the myqueue system.
 */

#ifndef MYQUEUE_ERRORS_H
#define MYQUEUE_ERRORS_H

#include <stdexcept>
#include <string>

namespace myqueue {

/**
 * @brief Error codes for myqueue operations
 * 
 * Categorized by type:
 * - 0: Success
 * - 100-199: Task errors
 * - 200-299: Resource errors
 * - 300-399: IPC errors
 * - 400-499: File errors
 */
enum class ErrorCode {
    SUCCESS = 0,
    
    // Task errors (100-199)
    TASK_NOT_FOUND = 100,
    TASK_SCRIPT_NOT_FOUND = 101,
    TASK_INVALID_STATE = 102,
    TASK_ALREADY_EXISTS = 103,
    
    // Resource errors (200-299)
    RESOURCE_UNAVAILABLE = 200,
    RESOURCE_INVALID_SPEC = 201,
    RESOURCE_ALLOCATION_FAILED = 202,
    
    // IPC errors (300-399)
    IPC_CONNECTION_FAILED = 300,
    IPC_SERVER_NOT_RUNNING = 301,
    IPC_SEND_FAILED = 302,
    IPC_RECEIVE_FAILED = 303,
    IPC_PROTOCOL_ERROR = 304,
    
    // File errors (400-499)
    FILE_NOT_FOUND = 400,
    FILE_PARSE_ERROR = 401,
    WORKDIR_NOT_FOUND = 402,
    FILE_WRITE_ERROR = 403,
    FILE_READ_ERROR = 404,
};

/**
 * @brief Convert ErrorCode to human-readable string
 * @param code The error code to convert
 * @return String representation of the error code
 */
inline std::string errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:
            return "Success";
        
        // Task errors
        case ErrorCode::TASK_NOT_FOUND:
            return "Task not found";
        case ErrorCode::TASK_SCRIPT_NOT_FOUND:
            return "Task script not found";
        case ErrorCode::TASK_INVALID_STATE:
            return "Invalid task state";
        case ErrorCode::TASK_ALREADY_EXISTS:
            return "Task already exists";
        
        // Resource errors
        case ErrorCode::RESOURCE_UNAVAILABLE:
            return "Resource unavailable";
        case ErrorCode::RESOURCE_INVALID_SPEC:
            return "Invalid resource specification";
        case ErrorCode::RESOURCE_ALLOCATION_FAILED:
            return "Resource allocation failed";
        
        // IPC errors
        case ErrorCode::IPC_CONNECTION_FAILED:
            return "IPC connection failed";
        case ErrorCode::IPC_SERVER_NOT_RUNNING:
            return "Server is not running";
        case ErrorCode::IPC_SEND_FAILED:
            return "Failed to send IPC message";
        case ErrorCode::IPC_RECEIVE_FAILED:
            return "Failed to receive IPC message";
        case ErrorCode::IPC_PROTOCOL_ERROR:
            return "IPC protocol error";
        
        // File errors
        case ErrorCode::FILE_NOT_FOUND:
            return "File not found";
        case ErrorCode::FILE_PARSE_ERROR:
            return "File parse error";
        case ErrorCode::WORKDIR_NOT_FOUND:
            return "Working directory not found";
        case ErrorCode::FILE_WRITE_ERROR:
            return "Failed to write file";
        case ErrorCode::FILE_READ_ERROR:
            return "Failed to read file";
        
        default:
            return "Unknown error";
    }
}

/**
 * @brief Custom exception class for myqueue errors
 * 
 * Provides structured error handling with error codes and messages.
 */
class MyQueueException : public std::runtime_error {
public:
    /**
     * @brief Construct exception with error code and message
     * @param code The error code
     * @param message Additional error message
     */
    MyQueueException(ErrorCode code, const std::string& message)
        : std::runtime_error(buildMessage(code, message))
        , code_(code)
        , message_(message) {}
    
    /**
     * @brief Construct exception with error code only
     * @param code The error code
     */
    explicit MyQueueException(ErrorCode code)
        : std::runtime_error(errorCodeToString(code))
        , code_(code)
        , message_() {}
    
    /**
     * @brief Get the error code
     * @return The error code associated with this exception
     */
    ErrorCode code() const noexcept { return code_; }
    
    /**
     * @brief Get the additional message
     * @return The additional message (may be empty)
     */
    const std::string& message() const noexcept { return message_; }

private:
    ErrorCode code_;
    std::string message_;
    
    static std::string buildMessage(ErrorCode code, const std::string& message) {
        std::string result = errorCodeToString(code);
        if (!message.empty()) {
            result += ": " + message;
        }
        return result;
    }
};

} // namespace myqueue

#endif // MYQUEUE_ERRORS_H

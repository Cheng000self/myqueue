/**
 * @file protocol.h
 * @brief IPC protocol definitions for myqueue
 * 
 * Defines the message types and data structures used for communication
 * between the CLI client and the server daemon via Unix Domain Socket.
 * All messages are serialized as JSON for simplicity and debugging.
 */

#ifndef MYQUEUE_PROTOCOL_H
#define MYQUEUE_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

namespace myqueue {

/**
 * @brief Message types for IPC communication
 * 
 * Request types (1-99):
 * - SUBMIT: Submit a new task
 * - QUERY_QUEUE: Query current queue status
 * - DELETE_TASK: Delete one or more tasks
 * - DELETE_ALL: Delete all tasks
 * - SHUTDOWN: Request server shutdown
 * 
 * Response types (100+):
 * - OK: Operation succeeded
 * - ERROR: Operation failed
 */
enum class MsgType : uint8_t {
    // Request types
    SUBMIT = 1,
    QUERY_QUEUE = 2,
    DELETE_TASK = 3,
    SHUTDOWN = 4,
    DELETE_ALL = 5,
    QUERY_QUEUE_ALL = 6,
    GET_TASK_INFO = 7,
    GET_TASK_LOG = 8,
    
    // Response types
    OK = 100,
    ERROR = 101,
};

/**
 * @brief Convert MsgType to string representation
 * @param type The message type to convert
 * @return String representation
 */
std::string msgTypeToString(MsgType type);

/**
 * @brief Parse MsgType from string
 * @param str String representation
 * @return Corresponding MsgType enum value
 * @throws std::invalid_argument if string is not recognized
 */
MsgType msgTypeFromString(const std::string& str);

/**
 * @brief Request to submit a new task
 * 
 * Contains all information needed to create a new task in the queue.
 */
struct SubmitRequest {
    /// Path to the script to execute
    std::string script_path;
    
    /// Working directory for task execution
    std::string workdir;
    
    /// Number of CPU cores requested
    int ncpu = 1;
    
    /// Number of GPU devices requested
    int ngpu = 1;
    
    /// Specific CPU cores requested (empty = auto-allocate)
    std::vector<int> specific_cpus;
    
    /// Specific GPU devices requested (empty = auto-allocate)
    std::vector<int> specific_gpus;
    
    /// Log file name for job output (empty = use server default)
    std::string log_file;
    
    /**
     * @brief Serialize request to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize request from JSON string
     * @param json JSON string to parse
     * @return SubmitRequest object
     * @throws MyQueueException if JSON is invalid
     */
    static SubmitRequest fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two requests
     * @param other Request to compare with
     * @return true if all fields are equal
     */
    bool operator==(const SubmitRequest& other) const;
    
    /**
     * @brief Check inequality of two requests
     * @param other Request to compare with
     * @return true if any field differs
     */
    bool operator!=(const SubmitRequest& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Request to delete one or more tasks
 * 
 * Contains a list of task IDs to delete. Both pending and running
 * tasks can be deleted.
 */
struct DeleteRequest {
    /// List of task IDs to delete
    std::vector<uint64_t> task_ids;
    
    /**
     * @brief Serialize request to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize request from JSON string
     * @param json JSON string to parse
     * @return DeleteRequest object
     * @throws MyQueueException if JSON is invalid
     */
    static DeleteRequest fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two requests
     * @param other Request to compare with
     * @return true if all fields are equal
     */
    bool operator==(const DeleteRequest& other) const;
    
    /**
     * @brief Check inequality of two requests
     * @param other Request to compare with
     * @return true if any field differs
     */
    bool operator!=(const DeleteRequest& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Information about a single task
 * 
 * Used in queue query responses to provide task details.
 */
struct TaskInfo {
    /// Unique task identifier
    uint64_t id = 0;
    
    /// Task status ("running" or "pending")
    std::string status;
    
    /// Path to the script
    std::string script;
    
    /// Working directory
    std::string workdir;
    
    /// Allocated CPU cores
    std::vector<int> cpus;
    
    /// Allocated GPU devices
    std::vector<int> gpus;
    
    /// Exit code (for completed tasks)
    int exit_code = 0;
    
    /// Duration in seconds (for completed tasks)
    int64_t duration_seconds = 0;
    
    /**
     * @brief Serialize task info to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize task info from JSON string
     * @param json JSON string to parse
     * @return TaskInfo object
     * @throws MyQueueException if JSON is invalid
     */
    static TaskInfo fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two task infos
     * @param other TaskInfo to compare with
     * @return true if all fields are equal
     */
    bool operator==(const TaskInfo& other) const;
    
    /**
     * @brief Check inequality of two task infos
     * @param other TaskInfo to compare with
     * @return true if any field differs
     */
    bool operator!=(const TaskInfo& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Response to queue query request
 * 
 * Contains lists of running and pending tasks.
 */
struct QueueResponse {
    /// List of currently running tasks
    std::vector<TaskInfo> running;
    
    /// List of pending tasks (in submission order)
    std::vector<TaskInfo> pending;
    
    /// List of completed/failed/cancelled tasks (only in "all" mode)
    std::vector<TaskInfo> completed;
    
    /**
     * @brief Serialize response to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize response from JSON string
     * @param json JSON string to parse
     * @return QueueResponse object
     * @throws MyQueueException if JSON is invalid
     */
    static QueueResponse fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two responses
     * @param other Response to compare with
     * @return true if all fields are equal
     */
    bool operator==(const QueueResponse& other) const;
    
    /**
     * @brief Check inequality of two responses
     * @param other Response to compare with
     * @return true if any field differs
     */
    bool operator!=(const QueueResponse& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Response to submit request
 * 
 * Contains the assigned task ID on success.
 */
struct SubmitResponse {
    /// Assigned task ID
    uint64_t task_id = 0;
    
    /**
     * @brief Serialize response to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize response from JSON string
     * @param json JSON string to parse
     * @return SubmitResponse object
     * @throws MyQueueException if JSON is invalid
     */
    static SubmitResponse fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two responses
     * @param other Response to compare with
     * @return true if all fields are equal
     */
    bool operator==(const SubmitResponse& other) const {
        return task_id == other.task_id;
    }
    
    /**
     * @brief Check inequality of two responses
     * @param other Response to compare with
     * @return true if any field differs
     */
    bool operator!=(const SubmitResponse& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Response to delete request
 * 
 * Contains results for each deletion attempt.
 */
struct DeleteResponse {
    /// Results for each task ID (true = deleted, false = not found/failed)
    std::vector<bool> results;
    
    /**
     * @brief Serialize response to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize response from JSON string
     * @param json JSON string to parse
     * @return DeleteResponse object
     * @throws MyQueueException if JSON is invalid
     */
    static DeleteResponse fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two responses
     * @param other Response to compare with
     * @return true if all fields are equal
     */
    bool operator==(const DeleteResponse& other) const {
        return results == other.results;
    }
    
    /**
     * @brief Check inequality of two responses
     * @param other Response to compare with
     * @return true if any field differs
     */
    bool operator!=(const DeleteResponse& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Error response
 * 
 * Contains error code and message when an operation fails.
 */
struct ErrorResponse {
    /// Error code
    int code = 0;
    
    /// Error message
    std::string message;
    
    /**
     * @brief Serialize response to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize response from JSON string
     * @param json JSON string to parse
     * @return ErrorResponse object
     * @throws MyQueueException if JSON is invalid
     */
    static ErrorResponse fromJson(const std::string& json);
    
    /**
     * @brief Check equality of two responses
     * @param other Response to compare with
     * @return true if all fields are equal
     */
    bool operator==(const ErrorResponse& other) const {
        return code == other.code && message == other.message;
    }
    
    /**
     * @brief Check inequality of two responses
     * @param other Response to compare with
     * @return true if any field differs
     */
    bool operator!=(const ErrorResponse& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Response to delete all request
 * 
 * Contains counts of deleted tasks by category.
 */
struct DeleteAllResponse {
    /// Total number of tasks deleted
    int deleted_count = 0;
    
    /// Number of running tasks terminated
    int running_terminated = 0;
    
    /// Number of pending tasks deleted
    int pending_deleted = 0;
    
    /// Number of completed tasks deleted
    int completed_deleted = 0;
    
    /**
     * @brief Serialize response to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize response from JSON string
     * @param json JSON string to parse
     * @return DeleteAllResponse object
     * @throws MyQueueException if JSON is invalid
     */
    static DeleteAllResponse fromJson(const std::string& json);
};

/**
 * @brief Request to get task info
 */
struct TaskInfoRequest {
    uint64_t task_id = 0;
    
    std::string toJson() const;
    static TaskInfoRequest fromJson(const std::string& json);
};

/**
 * @brief Detailed task information response
 */
struct TaskDetailResponse {
    uint64_t id = 0;
    std::string status;
    std::string script;
    std::string workdir;
    int ncpu = 0;
    int ngpu = 0;
    std::vector<int> specific_cpus;
    std::vector<int> specific_gpus;
    std::vector<int> allocated_cpus;
    std::vector<int> allocated_gpus;
    std::string log_file;
    int exit_code = 0;
    pid_t pid = 0;
    std::string submit_time;
    std::string start_time;
    std::string end_time;
    int64_t duration_seconds = 0;
    bool found = false;
    
    std::string toJson() const;
    static TaskDetailResponse fromJson(const std::string& json);
};

/**
 * @brief Request to get task log
 */
struct TaskLogRequest {
    uint64_t task_id = 0;
    int tail_lines = 0;  // 0 = all lines
    
    std::string toJson() const;
    static TaskLogRequest fromJson(const std::string& json);
};

/**
 * @brief Task log response
 */
struct TaskLogResponse {
    uint64_t task_id = 0;
    std::string log_path;
    std::string content;
    bool found = false;
    std::string error;
    
    std::string toJson() const;
    static TaskLogResponse fromJson(const std::string& json);
};

} // namespace myqueue

#endif // MYQUEUE_PROTOCOL_H

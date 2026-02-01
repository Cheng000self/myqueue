/**
 * @file task.h
 * @brief Task data structure and status definitions for myqueue
 * 
 * Defines the Task struct which represents a computation task in the queue,
 * including resource requirements, allocation status, and execution state.
 */

#ifndef MYQUEUE_TASK_H
#define MYQUEUE_TASK_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/types.h>

namespace myqueue {

/**
 * @brief Task execution status
 * 
 * Represents the lifecycle states of a task:
 * - PENDING: Waiting in queue for resources
 * - RUNNING: Currently executing
 * - COMPLETED: Finished successfully (exit code 0)
 * - FAILED: Finished with error (exit code != 0)
 * - CANCELLED: Deleted by user
 */
enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

/**
 * @brief Convert TaskStatus to string representation
 * @param status The task status to convert
 * @return String representation ("pending", "running", etc.)
 */
inline std::string taskStatusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING:   return "pending";
        case TaskStatus::RUNNING:   return "running";
        case TaskStatus::COMPLETED: return "completed";
        case TaskStatus::FAILED:    return "failed";
        case TaskStatus::CANCELLED: return "cancelled";
        default:                    return "unknown";
    }
}

/**
 * @brief Parse TaskStatus from string
 * @param str String representation of status
 * @return Corresponding TaskStatus enum value
 * @throws std::invalid_argument if string is not recognized
 */
inline TaskStatus taskStatusFromString(const std::string& str) {
    if (str == "pending")   return TaskStatus::PENDING;
    if (str == "running")   return TaskStatus::RUNNING;
    if (str == "completed") return TaskStatus::COMPLETED;
    if (str == "failed")    return TaskStatus::FAILED;
    if (str == "cancelled") return TaskStatus::CANCELLED;
    throw std::invalid_argument("Unknown task status: " + str);
}

/**
 * @brief Represents a computation task in the queue
 * 
 * Contains all information about a task including:
 * - Identification (id)
 * - Execution parameters (script_path, workdir)
 * - Resource requirements (ncpu, ngpu, specific_cpus, specific_gpus)
 * - Allocated resources (allocated_cpus, allocated_gpus)
 * - Execution state (status, pid, exit_code)
 * - Timestamps (submit_time, start_time, end_time)
 */
struct Task {
    /// Unique task identifier (monotonically increasing)
    uint64_t id = 0;
    
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
    
    /// Log file name for job output (empty = no log, default = "job.log")
    std::string log_file;
    
    /// CPU cores allocated to this task
    std::vector<int> allocated_cpus;
    
    /// GPU devices allocated to this task
    std::vector<int> allocated_gpus;
    
    /// Current task status
    TaskStatus status = TaskStatus::PENDING;
    
    /// Process ID when running (0 if not running)
    pid_t pid = 0;
    
    /// Exit code after completion (0 = success)
    int exit_code = 0;
    
    /// Time when task was submitted
    std::chrono::system_clock::time_point submit_time;
    
    /// Time when task started executing (nullopt if not started)
    std::optional<std::chrono::system_clock::time_point> start_time;
    
    /// Time when task finished (nullopt if not finished)
    std::optional<std::chrono::system_clock::time_point> end_time;
    
    /**
     * @brief Serialize task to JSON string
     * @return JSON string representation of the task
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize task from JSON string
     * @param json JSON string to parse
     * @return Task object
     * @throws std::runtime_error if JSON is invalid
     */
    static Task fromJson(const std::string& json);
    
    /**
     * @brief Check if task is in a terminal state
     * @return true if task is COMPLETED, FAILED, or CANCELLED
     */
    bool isTerminal() const {
        return status == TaskStatus::COMPLETED ||
               status == TaskStatus::FAILED ||
               status == TaskStatus::CANCELLED;
    }
    
    /**
     * @brief Check if task can be scheduled
     * @return true if task is PENDING
     */
    bool canSchedule() const {
        return status == TaskStatus::PENDING;
    }
    
    /**
     * @brief Check equality of two tasks
     * @param other Task to compare with
     * @return true if all fields are equal
     */
    bool operator==(const Task& other) const;
    
    /**
     * @brief Check inequality of two tasks
     * @param other Task to compare with
     * @return true if any field differs
     */
    bool operator!=(const Task& other) const {
        return !(*this == other);
    }
};

} // namespace myqueue

#endif // MYQUEUE_TASK_H

/**
 * @file task_queue.h
 * @brief TaskQueue class for managing computation tasks
 * 
 * Provides thread-safe task management including submission, querying,
 * status updates, and persistence.
 */

#ifndef MYQUEUE_TASK_QUEUE_H
#define MYQUEUE_TASK_QUEUE_H

#include "myqueue/task.h"
#include "myqueue/protocol.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace myqueue {

/**
 * @brief Manages the task queue with thread-safe operations
 * 
 * TaskQueue is responsible for:
 * - Task submission with unique ID assignment
 * - Task querying by ID or status
 * - Task status updates (running, completed, failed)
 * - Task persistence to/from disk
 * 
 * All public methods are thread-safe.
 */
class TaskQueue {
public:
    /**
     * @brief Construct a TaskQueue with specified data directory
     * @param data_dir Directory for storing task data (e.g., ~/.myqueue/hostname/)
     */
    explicit TaskQueue(const std::string& data_dir = "");
    
    /**
     * @brief Destructor
     */
    ~TaskQueue() = default;
    
    // Disable copy
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    
    // Enable move
    TaskQueue(TaskQueue&&) = default;
    TaskQueue& operator=(TaskQueue&&) = default;
    
    /**
     * @brief Submit a new task to the queue
     * @param req Submit request containing task parameters
     * @return Unique task ID assigned to the new task
     * 
     * The task is assigned a unique monotonically increasing ID and
     * added to the queue with PENDING status.
     */
    uint64_t submit(const SubmitRequest& req);
    
    /**
     * @brief Submit multiple tasks for batch processing
     * @param script Path to the script to execute
     * @param workdirs List of working directories
     * @param ncpu Number of CPUs per task
     * @param ngpu Number of GPUs per task
     * @return List of assigned task IDs
     */
    std::vector<uint64_t> submitBatch(const std::string& script,
                                       const std::vector<std::string>& workdirs,
                                       int ncpu, int ngpu);
    
    /**
     * @brief Parse a workdirs file and submit batch tasks
     * @param script Path to the script to execute
     * @param workdirs_file Path to file containing working directories
     * @param ncpu Number of CPUs per task
     * @param ngpu Number of GPUs per task
     * @return Pair of (assigned task IDs, invalid directories that were skipped)
     * 
     * The workdirs file format:
     * - Each non-empty line is treated as a directory path
     * - Lines starting with '#' are comments and ignored
     * - Empty lines are ignored
     * - Non-existent directories are skipped and reported
     */
    std::pair<std::vector<uint64_t>, std::vector<std::string>> 
    submitBatchFromFile(const std::string& script,
                        const std::string& workdirs_file,
                        int ncpu, int ngpu);
    
    /**
     * @brief Parse a workdirs file
     * @param filepath Path to the workdirs file
     * @return Pair of (valid directories, invalid directories)
     * 
     * Static utility function for parsing workdirs files.
     */
    static std::pair<std::vector<std::string>, std::vector<std::string>>
    parseWorkdirsFile(const std::string& filepath);
    
    /**
     * @brief Get a task by ID
     * @param id Task ID to look up
     * @return Task if found, nullopt otherwise
     */
    std::optional<Task> getTask(uint64_t id) const;
    
    /**
     * @brief Get all pending tasks
     * @return Vector of tasks with PENDING status, ordered by submission time
     */
    std::vector<Task> getPendingTasks() const;
    
    /**
     * @brief Get all running tasks
     * @return Vector of tasks with RUNNING status
     */
    std::vector<Task> getRunningTasks() const;
    
    /**
     * @brief Get all tasks (for persistence/debugging)
     * @return Vector of all tasks
     */
    std::vector<Task> getAllTasks() const;
    
    /**
     * @brief Set a task to running status
     * @param id Task ID
     * @param pid Process ID of the running task
     * @param cpus Allocated CPU cores
     * @param gpus Allocated GPU devices
     * @return true if task was updated, false if task not found or invalid state
     */
    bool setTaskRunning(uint64_t id, pid_t pid,
                        const std::vector<int>& cpus,
                        const std::vector<int>& gpus);
    
    /**
     * @brief Set a task to completed status
     * @param id Task ID
     * @param exit_code Exit code from the process
     * @return true if task was updated, false if task not found or invalid state
     */
    bool setTaskCompleted(uint64_t id, int exit_code);
    
    /**
     * @brief Set a task to failed status
     * @param id Task ID
     * @return true if task was updated, false if task not found or invalid state
     */
    bool setTaskFailed(uint64_t id);
    
    /**
     * @brief Delete a task from the queue
     * @param id Task ID to delete
     * @return true if task was deleted, false if not found
     * 
     * Note: For running tasks, the caller is responsible for terminating
     * the process before calling this method.
     */
    bool deleteTask(uint64_t id);
    
    /**
     * @brief Delete multiple tasks from the queue
     * @param ids List of task IDs to delete
     * @return Vector of results (true = deleted, false = not found)
     */
    std::vector<bool> deleteTasks(const std::vector<uint64_t>& ids);
    
    /**
     * @brief Parse a range string like "1-10" into individual IDs
     * @param range_str Range string (e.g., "5" or "1-10")
     * @return Vector of task IDs in the range
     */
    static std::vector<uint64_t> parseIdRange(const std::string& range_str);
    
    /**
     * @brief Save task queue to persistent storage
     * 
     * Saves all tasks to a JSON file in the data directory.
     */
    void save();
    
    /**
     * @brief Load task queue from persistent storage
     * 
     * Loads tasks from the JSON file in the data directory.
     * If the file doesn't exist, starts with an empty queue.
     */
    void load();
    
    /**
     * @brief Get the next task ID that will be assigned
     * @return Next task ID
     */
    uint64_t getNextId() const;
    
    /**
     * @brief Get the number of tasks in the queue
     * @return Total number of tasks
     */
    size_t size() const;
    
    /**
     * @brief Check if the queue is empty
     * @return true if no tasks in queue
     */
    bool empty() const;
    
    /**
     * @brief Clear all tasks from the queue
     * 
     * Warning: This does not terminate running processes.
     */
    void clear();

private:
    /// Data directory for persistence
    std::string data_dir_;
    
    /// Map of task ID to Task
    std::map<uint64_t, Task> tasks_;
    
    /// Next task ID to assign (monotonically increasing)
    uint64_t next_id_ = 1;
    
    /// Mutex for thread safety
    mutable std::mutex mutex_;
    
    /**
     * @brief Get the path to the tasks JSON file
     * @return Full path to tasks.json
     */
    std::string getTasksFilePath() const;
};

} // namespace myqueue

#endif // MYQUEUE_TASK_QUEUE_H

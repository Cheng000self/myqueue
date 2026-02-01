/**
 * @file executor.h
 * @brief Executor class for running tasks as child processes
 * 
 * Handles process creation, environment setup, and process management.
 */

#ifndef MYQUEUE_EXECUTOR_H
#define MYQUEUE_EXECUTOR_H

#include "myqueue/task.h"

#include <optional>
#include <string>
#include <vector>

namespace myqueue {

/**
 * @brief Result of checking a process status
 */
struct ProcessStatus {
    bool running;           ///< true if process is still running
    int exit_code;          ///< Exit code if process has terminated
    bool signaled;          ///< true if process was terminated by signal
    int signal_number;      ///< Signal number if signaled
};

/**
 * @brief Executes tasks as child processes
 * 
 * The Executor is responsible for:
 * - Forking child processes
 * - Setting up environment variables (CUDA_VISIBLE_DEVICES, MYQUEUE_CPUS, MYQUEUE_GPUS)
 * - Changing to the task's working directory
 * - Executing the task script via bash
 * - Monitoring process status
 * - Terminating processes
 */
class Executor {
public:
    /**
     * @brief Construct an Executor
     * @param log_dir Optional directory for task output logs
     * @param enable_job_log Whether to enable job log output to workdir
     */
    explicit Executor(const std::string& log_dir = "", bool enable_job_log = false);
    
    /**
     * @brief Destructor
     */
    ~Executor() = default;
    
    /**
     * @brief Execute a task with allocated resources
     * @param task The task to execute
     * @param cpus Allocated CPU cores
     * @param gpus Allocated GPU devices
     * @return Process ID of the child process, or -1 on failure
     * 
     * The child process will have the following environment variables set:
     * - CUDA_VISIBLE_DEVICES: comma-separated list of GPU IDs
     * - MYQUEUE_CPUS: comma-separated list of CPU core IDs
     * - MYQUEUE_GPUS: comma-separated list of GPU IDs
     */
    pid_t execute(const Task& task,
                  const std::vector<int>& cpus,
                  const std::vector<int>& gpus);
    
    /**
     * @brief Check the status of a process
     * @param pid Process ID to check
     * @return ProcessStatus with current state
     */
    ProcessStatus checkStatus(pid_t pid);
    
    /**
     * @brief Terminate a process
     * @param pid Process ID to terminate
     * @param force If true, use SIGKILL; otherwise use SIGTERM first
     * @return true if signal was sent successfully
     */
    bool terminate(pid_t pid, bool force = false);
    
    /**
     * @brief Wait for a process to terminate
     * @param pid Process ID to wait for
     * @param timeout_ms Maximum time to wait in milliseconds (0 = no wait, -1 = infinite)
     * @return Exit code if process terminated, nullopt if still running or error
     */
    std::optional<int> waitFor(pid_t pid, int timeout_ms = -1);
    
    /**
     * @brief Build the environment variable string for CPUs
     * @param cpus List of CPU core IDs
     * @return Comma-separated string of CPU IDs
     */
    static std::string buildCpuString(const std::vector<int>& cpus);
    
    /**
     * @brief Build the environment variable string for GPUs
     * @param gpus List of GPU device IDs
     * @return Comma-separated string of GPU IDs
     */
    static std::string buildGpuString(const std::vector<int>& gpus);

private:
    /**
     * @brief Set up environment variables in the child process
     * @param cpus Allocated CPU cores
     * @param gpus Allocated GPU devices
     */
    void setupEnvironment(const std::vector<int>& cpus,
                          const std::vector<int>& gpus);
    
    /**
     * @brief Redirect stdout/stderr to log files
     * @param task_id Task ID for naming log files
     */
    void setupLogging(uint64_t task_id);
    
    /**
     * @brief Create log directory recursively
     * @return true if directory exists or was created successfully
     */
    bool createLogDirectory();
    
    /**
     * @brief Get current timestamp string for logging
     * @return Formatted timestamp string
     */
    static std::string getTimestamp();
    
    /**
     * @brief Write a message to the server log file
     * @param level Log level (INFO, WARN, ERROR, DEBUG)
     * @param message Log message
     */
    void writeServerLog(const std::string& level, const std::string& message);
    
    /// Directory for task output logs
    std::string log_dir_;
    
    /// Whether to enable job log output to workdir
    bool enable_job_log_ = false;
};

} // namespace myqueue

#endif // MYQUEUE_EXECUTOR_H

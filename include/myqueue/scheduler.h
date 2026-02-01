/**
 * @file scheduler.h
 * @brief Scheduler class for managing task execution
 * 
 * Coordinates between TaskQueue, ResourceMonitor, and Executor to
 * schedule and run tasks.
 */

#ifndef MYQUEUE_SCHEDULER_H
#define MYQUEUE_SCHEDULER_H

#include "myqueue/task_queue.h"
#include "myqueue/resource_monitor.h"
#include "myqueue/executor.h"

#include <atomic>
#include <functional>
#include <thread>

namespace myqueue {

/**
 * @brief Callback type for task state changes
 */
using TaskStateCallback = std::function<void(uint64_t task_id, TaskStatus old_status, TaskStatus new_status)>;

/**
 * @brief Schedules and manages task execution
 * 
 * The Scheduler is responsible for:
 * - Running a scheduling loop that checks for pending tasks
 * - Allocating resources for tasks using ResourceMonitor
 * - Executing tasks using Executor
 * - Monitoring running tasks for completion
 * - Releasing resources when tasks complete
 * - FIFO ordering of task execution
 */
class Scheduler {
public:
    /**
     * @brief Construct a Scheduler
     * @param queue Reference to the TaskQueue
     * @param monitor Reference to the ResourceMonitor
     * @param executor Reference to the Executor
     * @param scheduling_interval_ms Interval between scheduling attempts (default 1000ms)
     * @param check_interval_ms Interval between process status checks (default 500ms)
     */
    Scheduler(TaskQueue& queue, 
              ResourceMonitor& monitor, 
              Executor& executor,
              int scheduling_interval_ms = 1000,
              int check_interval_ms = 500);
    
    /**
     * @brief Destructor - stops the scheduler if running
     */
    ~Scheduler();
    
    // Disable copy
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    
    /**
     * @brief Start the scheduling loop
     * 
     * Starts background threads for scheduling and process monitoring.
     */
    void start();
    
    /**
     * @brief Stop the scheduling loop
     * 
     * Stops the background threads. Does not terminate running tasks.
     */
    void stop();
    
    /**
     * @brief Check if the scheduler is running
     * @return true if scheduler is active
     */
    bool isRunning() const;
    
    /**
     * @brief Set callback for task state changes
     * @param callback Function to call when task state changes
     */
    void setStateCallback(TaskStateCallback callback);
    
    /**
     * @brief Manually trigger a scheduling attempt
     * 
     * Useful for testing or when immediate scheduling is needed.
     * @return true if a task was scheduled
     */
    bool scheduleOnce();
    
    /**
     * @brief Check all running tasks for completion
     * 
     * Useful for testing or manual process checking.
     */
    void checkRunningTasks();
    
    /**
     * @brief Terminate a specific task
     * @param task_id Task ID to terminate
     * @param force If true, use SIGKILL; otherwise use SIGTERM
     * @return true if task was found and termination signal sent
     */
    bool terminateTask(uint64_t task_id, bool force = false);
    
    /**
     * @brief Get the number of currently running tasks
     * @return Number of running tasks
     */
    size_t getRunningCount() const;

private:
    /**
     * @brief Main scheduling loop
     */
    void schedulingLoop();
    
    /**
     * @brief Process monitoring loop
     */
    void monitoringLoop();
    
    /**
     * @brief Try to schedule the next pending task
     * @return true if a task was scheduled
     */
    bool tryScheduleNext();
    
    /**
     * @brief Handle task completion
     * @param task_id Task ID that completed
     * @param exit_code Exit code from the process
     */
    void handleTaskCompletion(uint64_t task_id, int exit_code);
    
    /**
     * @brief Notify state change callback
     */
    void notifyStateChange(uint64_t task_id, TaskStatus old_status, TaskStatus new_status);
    
    /// Reference to task queue
    TaskQueue& queue_;
    
    /// Reference to resource monitor
    ResourceMonitor& monitor_;
    
    /// Reference to executor
    Executor& executor_;
    
    /// Scheduling interval in milliseconds
    int scheduling_interval_ms_;
    
    /// Process check interval in milliseconds
    int check_interval_ms_;
    
    /// Running flag
    std::atomic<bool> running_{false};
    
    /// Scheduling thread
    std::thread scheduler_thread_;
    
    /// Monitoring thread
    std::thread monitor_thread_;
    
    /// State change callback
    TaskStateCallback state_callback_;
    
    /// Mutex for callback
    mutable std::mutex callback_mutex_;
};

} // namespace myqueue

#endif // MYQUEUE_SCHEDULER_H

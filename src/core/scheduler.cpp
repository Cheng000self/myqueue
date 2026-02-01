/**
 * @file scheduler.cpp
 * @brief Implementation of Scheduler class
 * 
 * Manages task scheduling and execution lifecycle.
 */

#include "myqueue/scheduler.h"

#include <chrono>

namespace myqueue {

Scheduler::Scheduler(TaskQueue& queue, 
                     ResourceMonitor& monitor, 
                     Executor& executor,
                     int scheduling_interval_ms,
                     int check_interval_ms)
    : queue_(queue)
    , monitor_(monitor)
    , executor_(executor)
    , scheduling_interval_ms_(scheduling_interval_ms)
    , check_interval_ms_(check_interval_ms) {
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    
    scheduler_thread_ = std::thread(&Scheduler::schedulingLoop, this);
    monitor_thread_ = std::thread(&Scheduler::monitoringLoop, this);
}

void Scheduler::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

bool Scheduler::isRunning() const {
    return running_.load();
}

void Scheduler::setStateCallback(TaskStateCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    state_callback_ = std::move(callback);
}

void Scheduler::schedulingLoop() {
    while (running_.load()) {
        tryScheduleNext();
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(scheduling_interval_ms_));
    }
}

void Scheduler::monitoringLoop() {
    while (running_.load()) {
        checkRunningTasks();
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(check_interval_ms_));
    }
}

bool Scheduler::scheduleOnce() {
    return tryScheduleNext();
}

bool Scheduler::tryScheduleNext() {
    // Get pending tasks in FIFO order
    auto pending = queue_.getPendingTasks();
    if (pending.empty()) {
        return false;
    }
    
    // Try to schedule the first pending task
    const Task& task = pending.front();
    
    // Try to allocate resources
    auto allocation = monitor_.allocate(
        task.ncpu, task.ngpu,
        task.specific_cpus, task.specific_gpus);
    
    if (!allocation.has_value()) {
        // Resources not available
        return false;
    }
    
    const auto& cpus = allocation->cpus;
    const auto& gpus = allocation->gpus;
    
    // Execute the task
    pid_t pid = executor_.execute(task, cpus, gpus);
    if (pid <= 0) {
        // Execution failed, release resources
        monitor_.release(cpus, gpus);
        queue_.setTaskFailed(task.id);
        notifyStateChange(task.id, TaskStatus::PENDING, TaskStatus::FAILED);
        return false;
    }
    
    // Update task status
    TaskStatus old_status = task.status;
    if (queue_.setTaskRunning(task.id, pid, cpus, gpus)) {
        notifyStateChange(task.id, old_status, TaskStatus::RUNNING);
        
        // Save queue state
        queue_.save();
        return true;
    }
    
    // Failed to update status (shouldn't happen)
    executor_.terminate(pid, true);
    monitor_.release(cpus, gpus);
    return false;
}

void Scheduler::checkRunningTasks() {
    auto running = queue_.getRunningTasks();
    
    for (const auto& task : running) {
        if (task.pid <= 0) {
            continue;
        }
        
        auto status = executor_.checkStatus(task.pid);
        
        if (!status.running) {
            handleTaskCompletion(task.id, status.exit_code);
        }
    }
}

void Scheduler::handleTaskCompletion(uint64_t task_id, int exit_code) {
    auto task = queue_.getTask(task_id);
    if (!task.has_value()) {
        return;
    }
    
    // Release resources
    monitor_.release(task->allocated_cpus, task->allocated_gpus);
    
    // Update task status
    TaskStatus old_status = task->status;
    TaskStatus new_status;
    
    if (exit_code == 0) {
        queue_.setTaskCompleted(task_id, exit_code);
        new_status = TaskStatus::COMPLETED;
    } else {
        queue_.setTaskCompleted(task_id, exit_code);  // Still mark as completed with exit code
        new_status = TaskStatus::COMPLETED;
    }
    
    notifyStateChange(task_id, old_status, new_status);
    
    // Save queue state
    queue_.save();
}

bool Scheduler::terminateTask(uint64_t task_id, bool /* force */) {
    auto task = queue_.getTask(task_id);
    if (!task.has_value()) {
        return false;
    }
    
    if (task->status != TaskStatus::RUNNING || task->pid <= 0) {
        return false;
    }
    
    // First try graceful termination with SIGTERM
    bool result = executor_.terminate(task->pid, false);
    
    if (result) {
        // Wait for process to terminate gracefully
        auto exit_code = executor_.waitFor(task->pid, 2000);
        
        // If still running, force kill with SIGKILL
        if (!exit_code.has_value()) {
            executor_.terminate(task->pid, true);
            // Wait again after SIGKILL
            exit_code = executor_.waitFor(task->pid, 1000);
        }
        
        // Release resources
        monitor_.release(task->allocated_cpus, task->allocated_gpus);
        
        // Update task status
        TaskStatus old_status = task->status;
        queue_.deleteTask(task_id);  // This marks as CANCELLED
        notifyStateChange(task_id, old_status, TaskStatus::CANCELLED);
        
        // Save queue state
        queue_.save();
    }
    
    return result;
}

void Scheduler::notifyStateChange(uint64_t task_id, TaskStatus old_status, TaskStatus new_status) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (state_callback_) {
        state_callback_(task_id, old_status, new_status);
    }
}

size_t Scheduler::getRunningCount() const {
    return queue_.getRunningTasks().size();
}

} // namespace myqueue

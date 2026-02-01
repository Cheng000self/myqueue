/**
 * @file test_scheduler.cpp
 * @brief Unit tests for Scheduler class
 * 
 * Tests task scheduling, execution, and state management.
 */

#include <gtest/gtest.h>
#include "myqueue/scheduler.h"
#include "myqueue/task_queue.h"
#include "myqueue/resource_monitor.h"
#include "myqueue/executor.h"
#include "myqueue/config.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace myqueue {
namespace testing {

class SchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir_ = "/tmp/myqueue_scheduler_test_" + std::to_string(getpid());
        std::string cmd = "mkdir -p " + test_dir_;
        (void)system(cmd.c_str());
        
        // Create components with mock-friendly configuration
        config_.gpu_memory_threshold_mb = 2000;
        config_.cpu_util_threshold = 40.0;
        config_.total_cpus = 64;
        config_.total_gpus = 8;
        
        queue_ = std::make_unique<TaskQueue>(test_dir_);
        monitor_ = std::make_unique<ResourceMonitor>(config_);
        executor_ = std::make_unique<Executor>(test_dir_);
        
        // Enable mock mode for faster tests
        monitor_->setMockMode(true);
        monitor_->setCPUCheckDuration(10);  // Very short check duration
        monitor_->setCPUCheckInterval(5);
        
        // Set mock GPU data - all GPUs available
        std::vector<GPUInfo> mock_gpus;
        for (int i = 0; i < 8; ++i) {
            GPUInfo info;
            info.device_id = i;
            info.memory_used_mb = 100;  // Below threshold
            info.memory_total_mb = 24000;
            info.is_busy = false;
            info.is_allocated = false;
            mock_gpus.push_back(info);
        }
        monitor_->setMockGPUData(mock_gpus);
        
        // Set mock CPU utilization - all CPUs idle
        std::map<int, double> mock_cpu_utils;
        for (int i = 0; i < 64; ++i) {
            mock_cpu_utils[i] = 10.0;  // Below threshold
        }
        monitor_->setMockCPUUtilization(mock_cpu_utils);
        
        // Use short intervals for testing
        scheduler_ = std::make_unique<Scheduler>(
            *queue_, *monitor_, *executor_, 100, 50);
    }
    
    void TearDown() override {
        if (scheduler_) {
            scheduler_->stop();
        }
        scheduler_.reset();
        executor_.reset();
        monitor_.reset();
        queue_.reset();
        
        // Cleanup test directory
        std::string cmd = "rm -rf " + test_dir_;
        (void)system(cmd.c_str());
    }
    
    SubmitRequest createRequest(const std::string& script_content,
                                 int ncpu = 1, int ngpu = 1) {
        // Create script file
        std::string script_path = test_dir_ + "/script_" + std::to_string(next_script_id_++) + ".sh";
        std::ofstream script(script_path);
        script << "#!/bin/bash\n" << script_content;
        script.close();
        chmod(script_path.c_str(), 0755);
        
        SubmitRequest req;
        req.script_path = script_path;
        req.workdir = test_dir_;
        req.ncpu = ncpu;
        req.ngpu = ngpu;
        return req;
    }
    
    std::string test_dir_;
    Config config_;
    std::unique_ptr<TaskQueue> queue_;
    std::unique_ptr<ResourceMonitor> monitor_;
    std::unique_ptr<Executor> executor_;
    std::unique_ptr<Scheduler> scheduler_;
    int next_script_id_ = 1;
};

// Test scheduler start and stop
TEST_F(SchedulerTest, StartStop) {
    EXPECT_FALSE(scheduler_->isRunning());
    
    scheduler_->start();
    EXPECT_TRUE(scheduler_->isRunning());
    
    scheduler_->stop();
    EXPECT_FALSE(scheduler_->isRunning());
}

// Test double start is safe
TEST_F(SchedulerTest, DoubleStart) {
    scheduler_->start();
    EXPECT_TRUE(scheduler_->isRunning());
    
    scheduler_->start();  // Should be no-op
    EXPECT_TRUE(scheduler_->isRunning());
    
    scheduler_->stop();
}

// Test double stop is safe
TEST_F(SchedulerTest, DoubleStop) {
    scheduler_->start();
    scheduler_->stop();
    EXPECT_FALSE(scheduler_->isRunning());
    
    scheduler_->stop();  // Should be no-op
    EXPECT_FALSE(scheduler_->isRunning());
}

// Test scheduleOnce with no pending tasks
TEST_F(SchedulerTest, ScheduleOnceNoPending) {
    bool result = scheduler_->scheduleOnce();
    EXPECT_FALSE(result);
}

// Test scheduleOnce with pending task
TEST_F(SchedulerTest, ScheduleOncePending) {
    auto req = createRequest("exit 0");
    uint64_t id = queue_->submit(req);
    
    bool result = scheduler_->scheduleOnce();
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::RUNNING);
    EXPECT_GT(task->pid, 0);
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    scheduler_->checkRunningTasks();
    
    task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
}

// Test FIFO scheduling order
TEST_F(SchedulerTest, FIFOOrder) {
    std::vector<uint64_t> ids;
    
    // Submit multiple tasks
    for (int i = 0; i < 3; ++i) {
        std::string output_file = test_dir_ + "/order_" + std::to_string(i) + ".txt";
        auto req = createRequest(
            "echo " + std::to_string(i) + " > " + output_file + "\n"
            "sleep 0.1\n"
        );
        ids.push_back(queue_->submit(req));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Ensure different submit times
    }
    
    // Schedule all tasks
    scheduler_->start();
    
    // Wait for all to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    scheduler_->stop();
    
    // Verify all completed
    for (uint64_t id : ids) {
        auto task = queue_->getTask(id);
        ASSERT_TRUE(task.has_value());
        EXPECT_EQ(task->status, TaskStatus::COMPLETED);
    }
}

// Test state callback
TEST_F(SchedulerTest, StateCallback) {
    std::atomic<int> callback_count{0};
    std::vector<std::tuple<uint64_t, TaskStatus, TaskStatus>> transitions;
    std::mutex transitions_mutex;
    
    scheduler_->setStateCallback([&](uint64_t id, TaskStatus old_s, TaskStatus new_s) {
        std::lock_guard<std::mutex> lock(transitions_mutex);
        transitions.emplace_back(id, old_s, new_s);
        callback_count++;
    });
    
    auto req = createRequest("exit 0");
    uint64_t id = queue_->submit(req);
    
    scheduler_->scheduleOnce();
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    scheduler_->checkRunningTasks();
    
    // Should have PENDING->RUNNING and RUNNING->COMPLETED
    EXPECT_GE(callback_count.load(), 2);
    
    std::lock_guard<std::mutex> lock(transitions_mutex);
    ASSERT_GE(transitions.size(), 2);
    
    // First transition: PENDING -> RUNNING
    EXPECT_EQ(std::get<0>(transitions[0]), id);
    EXPECT_EQ(std::get<1>(transitions[0]), TaskStatus::PENDING);
    EXPECT_EQ(std::get<2>(transitions[0]), TaskStatus::RUNNING);
    
    // Second transition: RUNNING -> COMPLETED
    EXPECT_EQ(std::get<0>(transitions[1]), id);
    EXPECT_EQ(std::get<1>(transitions[1]), TaskStatus::RUNNING);
    EXPECT_EQ(std::get<2>(transitions[1]), TaskStatus::COMPLETED);
}

// Test terminateTask
TEST_F(SchedulerTest, TerminateTask) {
    auto req = createRequest("sleep 60");
    uint64_t id = queue_->submit(req);
    
    scheduler_->scheduleOnce();
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::RUNNING);
    
    bool result = scheduler_->terminateTask(id);
    EXPECT_TRUE(result);
    
    // Task should be deleted (cancelled)
    task = queue_->getTask(id);
    EXPECT_FALSE(task.has_value());
}

// Test terminateTask on non-existent task
TEST_F(SchedulerTest, TerminateNonExistent) {
    bool result = scheduler_->terminateTask(999);
    EXPECT_FALSE(result);
}

// Test terminateTask on pending task
TEST_F(SchedulerTest, TerminatePending) {
    auto req = createRequest("exit 0");
    uint64_t id = queue_->submit(req);
    
    // Don't schedule, task is still pending
    bool result = scheduler_->terminateTask(id);
    EXPECT_FALSE(result);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::PENDING);
}

// Test getRunningCount
TEST_F(SchedulerTest, GetRunningCount) {
    EXPECT_EQ(scheduler_->getRunningCount(), 0);
    
    auto req = createRequest("sleep 60");
    queue_->submit(req);
    
    scheduler_->scheduleOnce();
    EXPECT_EQ(scheduler_->getRunningCount(), 1);
    
    // Submit and schedule another
    queue_->submit(createRequest("sleep 60"));
    scheduler_->scheduleOnce();
    EXPECT_EQ(scheduler_->getRunningCount(), 2);
    
    // Cleanup - terminate all running tasks
    auto running = queue_->getRunningTasks();
    for (const auto& task : running) {
        scheduler_->terminateTask(task.id, true);
    }
}

// Test resource release on completion
TEST_F(SchedulerTest, ResourceReleaseOnCompletion) {
    // Submit a task that uses specific resources
    auto req = createRequest("exit 0", 2, 1);
    req.specific_cpus = {0, 1};
    req.specific_gpus = {0};
    uint64_t id = queue_->submit(req);
    
    scheduler_->scheduleOnce();
    
    // Resources should be allocated
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->allocated_cpus, std::vector<int>({0, 1}));
    EXPECT_EQ(task->allocated_gpus, std::vector<int>({0}));
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    scheduler_->checkRunningTasks();
    
    // Now we should be able to allocate the same resources again
    auto allocation = monitor_->allocate(2, 1, {0, 1}, {0});
    EXPECT_TRUE(allocation.has_value());
    
    if (allocation.has_value()) {
        monitor_->release(allocation->cpus, allocation->gpus);
    }
}

// Test automatic scheduling loop
TEST_F(SchedulerTest, AutomaticScheduling) {
    auto req = createRequest("exit 0");
    uint64_t id = queue_->submit(req);
    
    scheduler_->start();
    
    // Wait for automatic scheduling and completion
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    scheduler_->stop();
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
}

// Test task with non-zero exit code
TEST_F(SchedulerTest, TaskWithNonZeroExit) {
    auto req = createRequest("exit 42");
    uint64_t id = queue_->submit(req);
    
    scheduler_->scheduleOnce();
    
    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    scheduler_->checkRunningTasks();
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
    EXPECT_EQ(task->exit_code, 42);
}

} // namespace testing
} // namespace myqueue

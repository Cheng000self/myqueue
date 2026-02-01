/**
 * @file test_task.cpp
 * @brief Unit tests for Task data structure and JSON serialization
 */

#include <gtest/gtest.h>
#include "myqueue/task.h"
#include "myqueue/errors.h"

namespace myqueue {
namespace testing {

class TaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a sample task for testing
        sample_task_.id = 123;
        sample_task_.script_path = "/home/user/job.sh";
        sample_task_.workdir = "/home/user/calc";
        sample_task_.ncpu = 4;
        sample_task_.ngpu = 2;
        sample_task_.specific_cpus = {0, 1, 2, 3};
        sample_task_.specific_gpus = {0, 1};
        sample_task_.allocated_cpus = {0, 1, 2, 3};
        sample_task_.allocated_gpus = {0, 1};
        sample_task_.status = TaskStatus::RUNNING;
        sample_task_.pid = 12345;
        sample_task_.exit_code = 0;
        sample_task_.submit_time = std::chrono::system_clock::now();
        sample_task_.start_time = std::chrono::system_clock::now();
        // end_time is nullopt by default
    }
    
    Task sample_task_;
};

// Test TaskStatus to string conversion
TEST_F(TaskTest, TaskStatusToString) {
    EXPECT_EQ(taskStatusToString(TaskStatus::PENDING), "pending");
    EXPECT_EQ(taskStatusToString(TaskStatus::RUNNING), "running");
    EXPECT_EQ(taskStatusToString(TaskStatus::COMPLETED), "completed");
    EXPECT_EQ(taskStatusToString(TaskStatus::FAILED), "failed");
    EXPECT_EQ(taskStatusToString(TaskStatus::CANCELLED), "cancelled");
}

// Test TaskStatus from string conversion
TEST_F(TaskTest, TaskStatusFromString) {
    EXPECT_EQ(taskStatusFromString("pending"), TaskStatus::PENDING);
    EXPECT_EQ(taskStatusFromString("running"), TaskStatus::RUNNING);
    EXPECT_EQ(taskStatusFromString("completed"), TaskStatus::COMPLETED);
    EXPECT_EQ(taskStatusFromString("failed"), TaskStatus::FAILED);
    EXPECT_EQ(taskStatusFromString("cancelled"), TaskStatus::CANCELLED);
}

// Test invalid status string throws exception
TEST_F(TaskTest, TaskStatusFromStringInvalid) {
    EXPECT_THROW(taskStatusFromString("invalid"), std::invalid_argument);
    EXPECT_THROW(taskStatusFromString(""), std::invalid_argument);
}

// Test Task JSON serialization round-trip
TEST_F(TaskTest, JsonRoundTrip) {
    std::string json = sample_task_.toJson();
    Task restored = Task::fromJson(json);
    
    EXPECT_EQ(restored.id, sample_task_.id);
    EXPECT_EQ(restored.script_path, sample_task_.script_path);
    EXPECT_EQ(restored.workdir, sample_task_.workdir);
    EXPECT_EQ(restored.ncpu, sample_task_.ncpu);
    EXPECT_EQ(restored.ngpu, sample_task_.ngpu);
    EXPECT_EQ(restored.specific_cpus, sample_task_.specific_cpus);
    EXPECT_EQ(restored.specific_gpus, sample_task_.specific_gpus);
    EXPECT_EQ(restored.allocated_cpus, sample_task_.allocated_cpus);
    EXPECT_EQ(restored.allocated_gpus, sample_task_.allocated_gpus);
    EXPECT_EQ(restored.status, sample_task_.status);
    EXPECT_EQ(restored.pid, sample_task_.pid);
    EXPECT_EQ(restored.exit_code, sample_task_.exit_code);
    
    // Timestamps are compared with second precision
    EXPECT_TRUE(restored.start_time.has_value());
    EXPECT_FALSE(restored.end_time.has_value());
}

// Test Task JSON serialization with all optional fields
TEST_F(TaskTest, JsonRoundTripWithEndTime) {
    sample_task_.end_time = std::chrono::system_clock::now();
    sample_task_.status = TaskStatus::COMPLETED;
    sample_task_.exit_code = 0;
    
    std::string json = sample_task_.toJson();
    Task restored = Task::fromJson(json);
    
    EXPECT_TRUE(restored.end_time.has_value());
    EXPECT_EQ(restored.status, TaskStatus::COMPLETED);
}

// Test Task JSON serialization with minimal fields
TEST_F(TaskTest, JsonRoundTripMinimal) {
    Task minimal;
    minimal.id = 1;
    minimal.script_path = "test.sh";
    minimal.workdir = ".";
    minimal.ncpu = 1;
    minimal.ngpu = 1;
    minimal.status = TaskStatus::PENDING;
    minimal.submit_time = std::chrono::system_clock::now();
    
    std::string json = minimal.toJson();
    Task restored = Task::fromJson(json);
    
    EXPECT_EQ(restored.id, minimal.id);
    EXPECT_EQ(restored.script_path, minimal.script_path);
    EXPECT_EQ(restored.workdir, minimal.workdir);
    EXPECT_EQ(restored.status, TaskStatus::PENDING);
    EXPECT_FALSE(restored.start_time.has_value());
    EXPECT_FALSE(restored.end_time.has_value());
}

// Test Task equality operator
TEST_F(TaskTest, EqualityOperator) {
    Task copy = sample_task_;
    // Need to serialize and deserialize to normalize timestamps
    std::string json = sample_task_.toJson();
    Task t1 = Task::fromJson(json);
    Task t2 = Task::fromJson(json);
    
    EXPECT_EQ(t1, t2);
    
    t2.id = 999;
    EXPECT_NE(t1, t2);
}

// Test Task helper methods
TEST_F(TaskTest, HelperMethods) {
    Task pending;
    pending.status = TaskStatus::PENDING;
    EXPECT_TRUE(pending.canSchedule());
    EXPECT_FALSE(pending.isTerminal());
    
    Task running;
    running.status = TaskStatus::RUNNING;
    EXPECT_FALSE(running.canSchedule());
    EXPECT_FALSE(running.isTerminal());
    
    Task completed;
    completed.status = TaskStatus::COMPLETED;
    EXPECT_FALSE(completed.canSchedule());
    EXPECT_TRUE(completed.isTerminal());
    
    Task failed;
    failed.status = TaskStatus::FAILED;
    EXPECT_FALSE(failed.canSchedule());
    EXPECT_TRUE(failed.isTerminal());
    
    Task cancelled;
    cancelled.status = TaskStatus::CANCELLED;
    EXPECT_FALSE(cancelled.canSchedule());
    EXPECT_TRUE(cancelled.isTerminal());
}

// Test invalid JSON throws exception
TEST_F(TaskTest, InvalidJsonThrows) {
    EXPECT_THROW(Task::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(Task::fromJson("{}"), MyQueueException);
    EXPECT_THROW(Task::fromJson("{\"id\": 1}"), MyQueueException);
}

// Test ErrorCode to string conversion
TEST(ErrorsTest, ErrorCodeToString) {
    EXPECT_EQ(errorCodeToString(ErrorCode::SUCCESS), "Success");
    EXPECT_EQ(errorCodeToString(ErrorCode::TASK_NOT_FOUND), "Task not found");
    EXPECT_EQ(errorCodeToString(ErrorCode::RESOURCE_UNAVAILABLE), "Resource unavailable");
    EXPECT_EQ(errorCodeToString(ErrorCode::IPC_CONNECTION_FAILED), "IPC connection failed");
    EXPECT_EQ(errorCodeToString(ErrorCode::FILE_NOT_FOUND), "File not found");
}

// Test MyQueueException
TEST(ErrorsTest, MyQueueException) {
    MyQueueException ex1(ErrorCode::TASK_NOT_FOUND);
    EXPECT_EQ(ex1.code(), ErrorCode::TASK_NOT_FOUND);
    EXPECT_EQ(ex1.message(), "");
    EXPECT_EQ(std::string(ex1.what()), "Task not found");
    
    MyQueueException ex2(ErrorCode::FILE_PARSE_ERROR, "invalid format");
    EXPECT_EQ(ex2.code(), ErrorCode::FILE_PARSE_ERROR);
    EXPECT_EQ(ex2.message(), "invalid format");
    EXPECT_EQ(std::string(ex2.what()), "File parse error: invalid format");
}

} // namespace testing
} // namespace myqueue

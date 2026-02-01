/**
 * @file test_executor.cpp
 * @brief Unit tests for Executor class
 * 
 * Tests process execution, environment setup, and process management.
 */

#include <gtest/gtest.h>
#include "myqueue/executor.h"
#include "myqueue/task.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace myqueue {
namespace testing {

class ExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir_ = "/tmp/myqueue_executor_test_" + std::to_string(getpid());
        std::string cmd = "mkdir -p " + test_dir_;
        (void)system(cmd.c_str());
        
        executor_ = std::make_unique<Executor>(test_dir_);
    }
    
    void TearDown() override {
        executor_.reset();
        
        // Cleanup test directory
        std::string cmd = "rm -rf " + test_dir_;
        (void)system(cmd.c_str());
    }
    
    Task createTask(const std::string& script_content, 
                    const std::string& workdir = "") {
        Task task;
        task.id = next_task_id_++;
        task.workdir = workdir.empty() ? test_dir_ : workdir;
        task.script_path = test_dir_ + "/script_" + std::to_string(task.id) + ".sh";
        task.ncpu = 1;
        task.ngpu = 1;
        task.status = TaskStatus::PENDING;
        
        // Write script file
        std::ofstream script(task.script_path);
        script << "#!/bin/bash\n" << script_content;
        script.close();
        chmod(task.script_path.c_str(), 0755);
        
        return task;
    }
    
    std::string test_dir_;
    std::unique_ptr<Executor> executor_;
    uint64_t next_task_id_ = 1;
};

// Test buildCpuString
TEST_F(ExecutorTest, BuildCpuString) {
    EXPECT_EQ(Executor::buildCpuString({}), "");
    EXPECT_EQ(Executor::buildCpuString({0}), "0");
    EXPECT_EQ(Executor::buildCpuString({0, 1}), "0,1");
    EXPECT_EQ(Executor::buildCpuString({0, 1, 2, 3}), "0,1,2,3");
    EXPECT_EQ(Executor::buildCpuString({32, 33, 34, 35}), "32,33,34,35");
}

// Test buildGpuString
TEST_F(ExecutorTest, BuildGpuString) {
    EXPECT_EQ(Executor::buildGpuString({}), "");
    EXPECT_EQ(Executor::buildGpuString({0}), "0");
    EXPECT_EQ(Executor::buildGpuString({0, 1}), "0,1");
    EXPECT_EQ(Executor::buildGpuString({4, 5, 6, 7}), "4,5,6,7");
}

// Test simple script execution
TEST_F(ExecutorTest, ExecuteSimpleScript) {
    auto task = createTask("exit 0");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 0);
}

// Test script with non-zero exit code
TEST_F(ExecutorTest, ExecuteScriptWithError) {
    auto task = createTask("exit 42");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 42);
}

// Test environment variables are set correctly
TEST_F(ExecutorTest, EnvironmentVariables) {
    std::string output_file = test_dir_ + "/env_output.txt";
    auto task = createTask(
        "echo \"CUDA_VISIBLE_DEVICES=$CUDA_VISIBLE_DEVICES\" > " + output_file + "\n"
        "echo \"MYQUEUE_CPUS=$MYQUEUE_CPUS\" >> " + output_file + "\n"
        "echo \"MYQUEUE_GPUS=$MYQUEUE_GPUS\" >> " + output_file + "\n"
    );
    
    std::vector<int> cpus = {0, 1, 2, 3};
    std::vector<int> gpus = {0, 1};
    
    pid_t pid = executor_->execute(task, cpus, gpus);
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 0);
    
    // Read and verify output
    std::ifstream output(output_file);
    std::string line;
    
    std::getline(output, line);
    EXPECT_EQ(line, "CUDA_VISIBLE_DEVICES=0,1");
    
    std::getline(output, line);
    EXPECT_EQ(line, "MYQUEUE_CPUS=0,1,2,3");
    
    std::getline(output, line);
    EXPECT_EQ(line, "MYQUEUE_GPUS=0,1");
}

// Test working directory is set correctly
TEST_F(ExecutorTest, WorkingDirectory) {
    std::string output_file = test_dir_ + "/pwd_output.txt";
    auto task = createTask("pwd > " + output_file);
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 0);
    
    // Read and verify output
    std::ifstream output(output_file);
    std::string line;
    std::getline(output, line);
    EXPECT_EQ(line, test_dir_);
}

// Test checkStatus for running process
TEST_F(ExecutorTest, CheckStatusRunning) {
    auto task = createTask("sleep 10");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Process should be running
    auto status = executor_->checkStatus(pid);
    EXPECT_TRUE(status.running);
    
    // Terminate it
    executor_->terminate(pid, true);
    executor_->waitFor(pid, 1000);
}

// Test checkStatus for terminated process
TEST_F(ExecutorTest, CheckStatusTerminated) {
    auto task = createTask("exit 5");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Wait for completion - this reaps the process
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 5);
    
    // After waitFor, the process has been reaped, so checkStatus
    // will return running=false with exit_code=-1 (process not found)
    auto status = executor_->checkStatus(pid);
    EXPECT_FALSE(status.running);
}

// Test terminate with SIGTERM
TEST_F(ExecutorTest, TerminateSigterm) {
    auto task = createTask("sleep 60");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Verify running
    auto status = executor_->checkStatus(pid);
    EXPECT_TRUE(status.running);
    
    // Terminate with SIGTERM
    bool result = executor_->terminate(pid, false);
    EXPECT_TRUE(result);
    
    // Wait for termination
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    
    // Should have been signaled
    status = executor_->checkStatus(pid);
    EXPECT_FALSE(status.running);
}

// Test terminate with SIGKILL
TEST_F(ExecutorTest, TerminateSigkill) {
    // Script that ignores SIGTERM
    auto task = createTask(
        "trap '' SIGTERM\n"
        "sleep 60\n"
    );
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Give it time to set up trap
    usleep(100000);
    
    // Terminate with SIGKILL (force)
    bool result = executor_->terminate(pid, true);
    EXPECT_TRUE(result);
    
    // Wait for termination
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
}

// Test waitFor with timeout
TEST_F(ExecutorTest, WaitForTimeout) {
    auto task = createTask("sleep 60");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Wait with short timeout - should return nullopt
    auto result = executor_->waitFor(pid, 100);
    EXPECT_FALSE(result.has_value());
    
    // Cleanup
    executor_->terminate(pid, true);
    executor_->waitFor(pid, 1000);
}

// Test waitFor non-blocking (timeout = 0)
TEST_F(ExecutorTest, WaitForNonBlocking) {
    auto task = createTask("sleep 60");
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    // Non-blocking check - should return nullopt
    auto result = executor_->waitFor(pid, 0);
    EXPECT_FALSE(result.has_value());
    
    // Cleanup
    executor_->terminate(pid, true);
    executor_->waitFor(pid, 1000);
}

// Test execution with invalid working directory
TEST_F(ExecutorTest, InvalidWorkingDirectory) {
    Task task;
    task.id = 999;
    task.workdir = "/nonexistent/directory/12345";
    task.script_path = test_dir_ + "/script.sh";
    task.ncpu = 1;
    task.ngpu = 1;
    
    // Create a simple script
    std::ofstream script(task.script_path);
    script << "#!/bin/bash\nexit 0\n";
    script.close();
    chmod(task.script_path.c_str(), 0755);
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 127);  // chdir failure
}

// Test logging output
TEST_F(ExecutorTest, LoggingOutput) {
    auto task = createTask(
        "echo 'stdout message'\n"
        "echo 'stderr message' >&2\n"
    );
    
    pid_t pid = executor_->execute(task, {0}, {0});
    ASSERT_GT(pid, 0);
    
    auto exit_code = executor_->waitFor(pid, 5000);
    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 0);
    
    // Check stdout log
    std::string stdout_path = test_dir_ + "/task_" + std::to_string(task.id) + ".out";
    std::ifstream stdout_file(stdout_path);
    ASSERT_TRUE(stdout_file.is_open());
    std::string stdout_content;
    std::getline(stdout_file, stdout_content);
    EXPECT_EQ(stdout_content, "stdout message");
    
    // Check stderr log
    std::string stderr_path = test_dir_ + "/task_" + std::to_string(task.id) + ".err";
    std::ifstream stderr_file(stderr_path);
    ASSERT_TRUE(stderr_file.is_open());
    std::string stderr_content;
    std::getline(stderr_file, stderr_content);
    EXPECT_EQ(stderr_content, "stderr message");
}

// Test multiple concurrent executions
TEST_F(ExecutorTest, ConcurrentExecutions) {
    const int num_tasks = 5;
    std::vector<pid_t> pids;
    std::vector<Task> tasks;
    
    for (int i = 0; i < num_tasks; ++i) {
        auto task = createTask("sleep 0.1 && exit " + std::to_string(i));
        tasks.push_back(task);
        
        pid_t pid = executor_->execute(task, {i % 32}, {i % 8});
        ASSERT_GT(pid, 0);
        pids.push_back(pid);
    }
    
    // Wait for all to complete
    for (int i = 0; i < num_tasks; ++i) {
        auto exit_code = executor_->waitFor(pids[i], 5000);
        ASSERT_TRUE(exit_code.has_value());
        EXPECT_EQ(*exit_code, i);
    }
}

} // namespace testing
} // namespace myqueue

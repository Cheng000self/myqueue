/**
 * @file test_task_queue.cpp
 * @brief Unit tests for TaskQueue class
 * 
 * Tests task submission, querying, status transitions, and ID uniqueness.
 */

#include <gtest/gtest.h>
#include "myqueue/task_queue.h"
#include "myqueue/errors.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <thread>
#include <unistd.h>
#include <vector>

namespace myqueue {
namespace testing {

class TaskQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a TaskQueue without persistence for testing
        queue_ = std::make_unique<TaskQueue>();
    }
    
    void TearDown() override {
        queue_.reset();
    }
    
    SubmitRequest createRequest(const std::string& script = "test.sh",
                                 const std::string& workdir = "/tmp/test",
                                 int ncpu = 1, int ngpu = 1) {
        SubmitRequest req;
        req.script_path = script;
        req.workdir = workdir;
        req.ncpu = ncpu;
        req.ngpu = ngpu;
        return req;
    }
    
    std::unique_ptr<TaskQueue> queue_;
};

// Test basic task submission
TEST_F(TaskQueueTest, SubmitTask) {
    auto req = createRequest();
    uint64_t id = queue_->submit(req);
    
    EXPECT_EQ(id, 1);
    EXPECT_EQ(queue_->size(), 1);
    EXPECT_FALSE(queue_->empty());
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->id, id);
    EXPECT_EQ(task->script_path, req.script_path);
    EXPECT_EQ(task->workdir, req.workdir);
    EXPECT_EQ(task->ncpu, req.ncpu);
    EXPECT_EQ(task->ngpu, req.ngpu);
    EXPECT_EQ(task->status, TaskStatus::PENDING);
}

// Test task ID uniqueness
TEST_F(TaskQueueTest, UniqueTaskIDs) {
    std::set<uint64_t> ids;
    
    for (int i = 0; i < 100; ++i) {
        auto req = createRequest("script" + std::to_string(i) + ".sh");
        uint64_t id = queue_->submit(req);
        
        // ID should not already exist
        EXPECT_EQ(ids.count(id), 0) << "Duplicate ID: " << id;
        ids.insert(id);
    }
    
    EXPECT_EQ(ids.size(), 100);
    EXPECT_EQ(queue_->size(), 100);
}

// Test task IDs are monotonically increasing
TEST_F(TaskQueueTest, MonotonicallyIncreasingIDs) {
    uint64_t prev_id = 0;
    
    for (int i = 0; i < 50; ++i) {
        auto req = createRequest();
        uint64_t id = queue_->submit(req);
        
        EXPECT_GT(id, prev_id) << "ID not monotonically increasing";
        prev_id = id;
    }
}

// Test getting non-existent task
TEST_F(TaskQueueTest, GetNonExistentTask) {
    auto task = queue_->getTask(999);
    EXPECT_FALSE(task.has_value());
}

// Test getting pending tasks
TEST_F(TaskQueueTest, GetPendingTasks) {
    // Submit multiple tasks
    for (int i = 0; i < 5; ++i) {
        queue_->submit(createRequest("script" + std::to_string(i) + ".sh"));
    }
    
    auto pending = queue_->getPendingTasks();
    EXPECT_EQ(pending.size(), 5);
    
    // All should be pending
    for (const auto& task : pending) {
        EXPECT_EQ(task.status, TaskStatus::PENDING);
    }
}

// Test pending tasks are ordered by submission time (FIFO)
TEST_F(TaskQueueTest, PendingTasksFIFOOrder) {
    std::vector<uint64_t> submitted_ids;
    
    for (int i = 0; i < 10; ++i) {
        uint64_t id = queue_->submit(createRequest("script" + std::to_string(i) + ".sh"));
        submitted_ids.push_back(id);
    }
    
    auto pending = queue_->getPendingTasks();
    ASSERT_EQ(pending.size(), 10);
    
    // Tasks should be in submission order
    for (size_t i = 0; i < pending.size(); ++i) {
        EXPECT_EQ(pending[i].id, submitted_ids[i]);
    }
}

// Test getting running tasks
TEST_F(TaskQueueTest, GetRunningTasks) {
    // Submit tasks
    uint64_t id1 = queue_->submit(createRequest("script1.sh"));
    uint64_t id2 = queue_->submit(createRequest("script2.sh"));
    uint64_t id3 = queue_->submit(createRequest("script3.sh"));
    
    // Set some to running
    queue_->setTaskRunning(id1, 1001, {0, 1}, {0});
    queue_->setTaskRunning(id3, 1003, {2, 3}, {1});
    
    auto running = queue_->getRunningTasks();
    EXPECT_EQ(running.size(), 2);
    
    auto pending = queue_->getPendingTasks();
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].id, id2);
}

// Test setTaskRunning
TEST_F(TaskQueueTest, SetTaskRunning) {
    uint64_t id = queue_->submit(createRequest());
    
    std::vector<int> cpus = {0, 1, 2, 3};
    std::vector<int> gpus = {0, 1};
    pid_t pid = 12345;
    
    bool result = queue_->setTaskRunning(id, pid, cpus, gpus);
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::RUNNING);
    EXPECT_EQ(task->pid, pid);
    EXPECT_EQ(task->allocated_cpus, cpus);
    EXPECT_EQ(task->allocated_gpus, gpus);
    EXPECT_TRUE(task->start_time.has_value());
}

// Test setTaskRunning on non-existent task
TEST_F(TaskQueueTest, SetTaskRunningNonExistent) {
    bool result = queue_->setTaskRunning(999, 12345, {0}, {0});
    EXPECT_FALSE(result);
}

// Test setTaskRunning on already running task
TEST_F(TaskQueueTest, SetTaskRunningAlreadyRunning) {
    uint64_t id = queue_->submit(createRequest());
    queue_->setTaskRunning(id, 12345, {0}, {0});
    
    // Try to set running again
    bool result = queue_->setTaskRunning(id, 12346, {1}, {1});
    EXPECT_FALSE(result);
    
    // Original values should be preserved
    auto task = queue_->getTask(id);
    EXPECT_EQ(task->pid, 12345);
}

// Test setTaskCompleted
TEST_F(TaskQueueTest, SetTaskCompleted) {
    uint64_t id = queue_->submit(createRequest());
    queue_->setTaskRunning(id, 12345, {0}, {0});
    
    bool result = queue_->setTaskCompleted(id, 0);
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
    EXPECT_EQ(task->exit_code, 0);
    EXPECT_TRUE(task->end_time.has_value());
}

// Test setTaskCompleted with non-zero exit code
TEST_F(TaskQueueTest, SetTaskCompletedWithError) {
    uint64_t id = queue_->submit(createRequest());
    queue_->setTaskRunning(id, 12345, {0}, {0});
    
    bool result = queue_->setTaskCompleted(id, 1);
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
    EXPECT_EQ(task->exit_code, 1);
}

// Test setTaskCompleted on pending task (should fail)
TEST_F(TaskQueueTest, SetTaskCompletedOnPending) {
    uint64_t id = queue_->submit(createRequest());
    
    bool result = queue_->setTaskCompleted(id, 0);
    EXPECT_FALSE(result);
    
    auto task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::PENDING);
}

// Test setTaskFailed
TEST_F(TaskQueueTest, SetTaskFailed) {
    uint64_t id = queue_->submit(createRequest());
    queue_->setTaskRunning(id, 12345, {0}, {0});
    
    bool result = queue_->setTaskFailed(id);
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->status, TaskStatus::FAILED);
    EXPECT_TRUE(task->end_time.has_value());
}

// Test setTaskFailed on pending task
TEST_F(TaskQueueTest, SetTaskFailedOnPending) {
    uint64_t id = queue_->submit(createRequest());
    
    bool result = queue_->setTaskFailed(id);
    EXPECT_TRUE(result);
    
    auto task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::FAILED);
}

// Test deleteTask
TEST_F(TaskQueueTest, DeleteTask) {
    uint64_t id = queue_->submit(createRequest());
    EXPECT_EQ(queue_->size(), 1);
    
    bool result = queue_->deleteTask(id);
    EXPECT_TRUE(result);
    EXPECT_EQ(queue_->size(), 0);
    EXPECT_TRUE(queue_->empty());
    
    auto task = queue_->getTask(id);
    EXPECT_FALSE(task.has_value());
}

// Test deleteTask on non-existent task
TEST_F(TaskQueueTest, DeleteNonExistentTask) {
    bool result = queue_->deleteTask(999);
    EXPECT_FALSE(result);
}

// Test deleteTasks (batch delete)
TEST_F(TaskQueueTest, DeleteTasks) {
    uint64_t id1 = queue_->submit(createRequest("script1.sh"));
    uint64_t id2 = queue_->submit(createRequest("script2.sh"));
    uint64_t id3 = queue_->submit(createRequest("script3.sh"));
    
    std::vector<uint64_t> to_delete = {id1, 999, id3};  // 999 doesn't exist
    auto results = queue_->deleteTasks(to_delete);
    
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results[0]);   // id1 deleted
    EXPECT_FALSE(results[1]);  // 999 not found
    EXPECT_TRUE(results[2]);   // id3 deleted
    
    EXPECT_EQ(queue_->size(), 1);
    EXPECT_TRUE(queue_->getTask(id2).has_value());
}

// Test submitBatch
TEST_F(TaskQueueTest, SubmitBatch) {
    std::vector<std::string> workdirs = {"/tmp/calc1", "/tmp/calc2", "/tmp/calc3"};
    
    auto ids = queue_->submitBatch("job.sh", workdirs, 2, 1);
    
    ASSERT_EQ(ids.size(), 3);
    EXPECT_EQ(queue_->size(), 3);
    
    for (size_t i = 0; i < ids.size(); ++i) {
        auto task = queue_->getTask(ids[i]);
        ASSERT_TRUE(task.has_value());
        EXPECT_EQ(task->script_path, "job.sh");
        EXPECT_EQ(task->workdir, workdirs[i]);
        EXPECT_EQ(task->ncpu, 2);
        EXPECT_EQ(task->ngpu, 1);
    }
}

// Test specific CPUs and GPUs in submit request
TEST_F(TaskQueueTest, SubmitWithSpecificResources) {
    SubmitRequest req;
    req.script_path = "test.sh";
    req.workdir = "/tmp/test";
    req.ncpu = 4;
    req.ngpu = 2;
    req.specific_cpus = {0, 1, 2, 3};
    req.specific_gpus = {0, 1};
    
    uint64_t id = queue_->submit(req);
    
    auto task = queue_->getTask(id);
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->specific_cpus, req.specific_cpus);
    EXPECT_EQ(task->specific_gpus, req.specific_gpus);
}

// Test clear
TEST_F(TaskQueueTest, Clear) {
    for (int i = 0; i < 10; ++i) {
        queue_->submit(createRequest());
    }
    EXPECT_EQ(queue_->size(), 10);
    
    queue_->clear();
    EXPECT_EQ(queue_->size(), 0);
    EXPECT_TRUE(queue_->empty());
    
    // Next ID should reset to 1
    uint64_t id = queue_->submit(createRequest());
    EXPECT_EQ(id, 1);
}

// Test getNextId
TEST_F(TaskQueueTest, GetNextId) {
    EXPECT_EQ(queue_->getNextId(), 1);
    
    queue_->submit(createRequest());
    EXPECT_EQ(queue_->getNextId(), 2);
    
    queue_->submit(createRequest());
    queue_->submit(createRequest());
    EXPECT_EQ(queue_->getNextId(), 4);
}

// Test getAllTasks
TEST_F(TaskQueueTest, GetAllTasks) {
    uint64_t id1 = queue_->submit(createRequest("script1.sh"));
    (void)queue_->submit(createRequest("script2.sh"));  // id2 not used
    queue_->setTaskRunning(id1, 12345, {0}, {0});
    queue_->setTaskCompleted(id1, 0);
    
    auto all = queue_->getAllTasks();
    EXPECT_EQ(all.size(), 2);
}

// Test parseWorkdirsFile with valid directories
TEST_F(TaskQueueTest, ParseWorkdirsFileValidDirs) {
    // Create a temporary workdirs file
    std::string workdirs_file = "/tmp/test_workdirs.txt";
    std::ofstream file(workdirs_file);
    file << "/tmp\n";
    file << "/var\n";
    file << "/usr\n";
    file.close();
    
    auto [valid, invalid] = TaskQueue::parseWorkdirsFile(workdirs_file);
    
    EXPECT_EQ(valid.size(), 3);
    EXPECT_EQ(invalid.size(), 0);
    EXPECT_EQ(valid[0], "/tmp");
    EXPECT_EQ(valid[1], "/var");
    EXPECT_EQ(valid[2], "/usr");
    
    std::remove(workdirs_file.c_str());
}

// Test parseWorkdirsFile with comments and empty lines
TEST_F(TaskQueueTest, ParseWorkdirsFileCommentsAndEmptyLines) {
    std::string workdirs_file = "/tmp/test_workdirs_comments.txt";
    std::ofstream file(workdirs_file);
    file << "# This is a comment\n";
    file << "/tmp\n";
    file << "\n";
    file << "  # Another comment with leading spaces\n";
    file << "/var\n";
    file << "   \n";  // Whitespace only line
    file << "/usr\n";
    file.close();
    
    auto [valid, invalid] = TaskQueue::parseWorkdirsFile(workdirs_file);
    
    EXPECT_EQ(valid.size(), 3);
    EXPECT_EQ(invalid.size(), 0);
    
    std::remove(workdirs_file.c_str());
}

// Test parseWorkdirsFile with invalid directories
TEST_F(TaskQueueTest, ParseWorkdirsFileInvalidDirs) {
    std::string workdirs_file = "/tmp/test_workdirs_invalid.txt";
    std::ofstream file(workdirs_file);
    file << "/tmp\n";
    file << "/nonexistent/path/12345\n";
    file << "/another/fake/path\n";
    file << "/var\n";
    file.close();
    
    auto [valid, invalid] = TaskQueue::parseWorkdirsFile(workdirs_file);
    
    EXPECT_EQ(valid.size(), 2);
    EXPECT_EQ(invalid.size(), 2);
    EXPECT_EQ(valid[0], "/tmp");
    EXPECT_EQ(valid[1], "/var");
    EXPECT_EQ(invalid[0], "/nonexistent/path/12345");
    EXPECT_EQ(invalid[1], "/another/fake/path");
    
    std::remove(workdirs_file.c_str());
}

// Test parseWorkdirsFile with whitespace trimming
TEST_F(TaskQueueTest, ParseWorkdirsFileWhitespaceTrimming) {
    std::string workdirs_file = "/tmp/test_workdirs_whitespace.txt";
    std::ofstream file(workdirs_file);
    file << "  /tmp  \n";
    file << "\t/var\t\n";
    file << "   /usr   \n";
    file.close();
    
    auto [valid, invalid] = TaskQueue::parseWorkdirsFile(workdirs_file);
    
    EXPECT_EQ(valid.size(), 3);
    EXPECT_EQ(valid[0], "/tmp");
    EXPECT_EQ(valid[1], "/var");
    EXPECT_EQ(valid[2], "/usr");
    
    std::remove(workdirs_file.c_str());
}

// Test parseWorkdirsFile with non-existent file
TEST_F(TaskQueueTest, ParseWorkdirsFileNonExistent) {
    auto [valid, invalid] = TaskQueue::parseWorkdirsFile("/nonexistent/file.txt");
    
    EXPECT_EQ(valid.size(), 0);
    EXPECT_EQ(invalid.size(), 0);
}

// Test submitBatchFromFile
TEST_F(TaskQueueTest, SubmitBatchFromFile) {
    std::string workdirs_file = "/tmp/test_workdirs_submit.txt";
    std::ofstream file(workdirs_file);
    file << "# Batch job directories\n";
    file << "/tmp\n";
    file << "/nonexistent/path\n";
    file << "/var\n";
    file.close();
    
    auto [ids, invalid] = queue_->submitBatchFromFile("job.sh", workdirs_file, 2, 1);
    
    EXPECT_EQ(ids.size(), 2);  // Only valid directories
    EXPECT_EQ(invalid.size(), 1);
    EXPECT_EQ(invalid[0], "/nonexistent/path");
    EXPECT_EQ(queue_->size(), 2);
    
    // Verify tasks were created correctly
    auto task1 = queue_->getTask(ids[0]);
    ASSERT_TRUE(task1.has_value());
    EXPECT_EQ(task1->script_path, "job.sh");
    EXPECT_EQ(task1->workdir, "/tmp");
    EXPECT_EQ(task1->ncpu, 2);
    EXPECT_EQ(task1->ngpu, 1);
    
    std::remove(workdirs_file.c_str());
}

// Test parseIdRange with single ID
TEST_F(TaskQueueTest, ParseIdRangeSingle) {
    auto ids = TaskQueue::parseIdRange("5");
    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids[0], 5);
}

// Test parseIdRange with range
TEST_F(TaskQueueTest, ParseIdRangeRange) {
    auto ids = TaskQueue::parseIdRange("1-5");
    ASSERT_EQ(ids.size(), 5);
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2);
    EXPECT_EQ(ids[2], 3);
    EXPECT_EQ(ids[3], 4);
    EXPECT_EQ(ids[4], 5);
}

// Test parseIdRange with same start and end
TEST_F(TaskQueueTest, ParseIdRangeSameStartEnd) {
    auto ids = TaskQueue::parseIdRange("10-10");
    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids[0], 10);
}

// Test parseIdRange with invalid format
TEST_F(TaskQueueTest, ParseIdRangeInvalid) {
    auto ids1 = TaskQueue::parseIdRange("abc");
    EXPECT_EQ(ids1.size(), 0);
    
    // "-5" is parsed as a single number attempt (fails due to negative)
    auto ids2 = TaskQueue::parseIdRange("-5");
    // This may or may not parse depending on implementation
    
    // "5-" is parsed as "5" (single number before dash)
    auto ids3 = TaskQueue::parseIdRange("5-");
    // This may parse as single number 5
    
    auto ids4 = TaskQueue::parseIdRange("");
    EXPECT_EQ(ids4.size(), 0);
}

// Test parseIdRange with reversed range (should return empty)
TEST_F(TaskQueueTest, ParseIdRangeReversed) {
    auto ids = TaskQueue::parseIdRange("10-5");
    EXPECT_EQ(ids.size(), 0);
}

// Test delete with range
TEST_F(TaskQueueTest, DeleteWithRange) {
    // Submit 10 tasks
    for (int i = 0; i < 10; ++i) {
        queue_->submit(createRequest("script" + std::to_string(i) + ".sh"));
    }
    EXPECT_EQ(queue_->size(), 10);
    
    // Delete range 3-7
    auto ids_to_delete = TaskQueue::parseIdRange("3-7");
    auto results = queue_->deleteTasks(ids_to_delete);
    
    EXPECT_EQ(results.size(), 5);
    for (bool r : results) {
        EXPECT_TRUE(r);
    }
    
    EXPECT_EQ(queue_->size(), 5);
    
    // Verify remaining tasks
    EXPECT_TRUE(queue_->getTask(1).has_value());
    EXPECT_TRUE(queue_->getTask(2).has_value());
    EXPECT_FALSE(queue_->getTask(3).has_value());
    EXPECT_FALSE(queue_->getTask(7).has_value());
    EXPECT_TRUE(queue_->getTask(8).has_value());
    EXPECT_TRUE(queue_->getTask(9).has_value());
    EXPECT_TRUE(queue_->getTask(10).has_value());
}

// Test persistence - save and load
TEST_F(TaskQueueTest, SaveAndLoad) {
    std::string test_dir = "/tmp/myqueue_test_" + std::to_string(getpid());
    
    // Create queue with persistence
    TaskQueue queue1(test_dir);
    
    // Submit some tasks
    auto req1 = createRequest("script1.sh", "/tmp/work1", 2, 1);
    req1.specific_cpus = {0, 1};
    req1.specific_gpus = {0};
    uint64_t id1 = queue1.submit(req1);
    
    auto req2 = createRequest("script2.sh", "/tmp/work2", 4, 2);
    uint64_t id2 = queue1.submit(req2);
    
    // Set one task to running
    queue1.setTaskRunning(id1, 12345, {0, 1}, {0});
    
    // Save
    queue1.save();
    
    // Create new queue and load
    TaskQueue queue2(test_dir);
    queue2.load();
    
    // Verify tasks were loaded correctly
    EXPECT_EQ(queue2.size(), 2);
    EXPECT_EQ(queue2.getNextId(), 3);
    
    auto task1 = queue2.getTask(id1);
    ASSERT_TRUE(task1.has_value());
    EXPECT_EQ(task1->script_path, "script1.sh");
    EXPECT_EQ(task1->workdir, "/tmp/work1");
    EXPECT_EQ(task1->ncpu, 2);
    EXPECT_EQ(task1->ngpu, 1);
    EXPECT_EQ(task1->status, TaskStatus::RUNNING);
    EXPECT_EQ(task1->pid, 12345);
    EXPECT_EQ(task1->allocated_cpus, std::vector<int>({0, 1}));
    EXPECT_EQ(task1->allocated_gpus, std::vector<int>({0}));
    
    auto task2 = queue2.getTask(id2);
    ASSERT_TRUE(task2.has_value());
    EXPECT_EQ(task2->script_path, "script2.sh");
    EXPECT_EQ(task2->status, TaskStatus::PENDING);
    
    // Cleanup
    std::remove((test_dir + "/tasks.json").c_str());
    rmdir(test_dir.c_str());
}

// Test load from non-existent file
TEST_F(TaskQueueTest, LoadNonExistent) {
    std::string test_dir = "/tmp/myqueue_nonexistent_" + std::to_string(getpid());
    
    TaskQueue queue(test_dir);
    queue.load();  // Should not throw
    
    EXPECT_EQ(queue.size(), 0);
    EXPECT_EQ(queue.getNextId(), 1);
}

// Test persistence preserves task IDs after restart
TEST_F(TaskQueueTest, PersistencePreservesNextId) {
    std::string test_dir = "/tmp/myqueue_nextid_" + std::to_string(getpid());
    
    {
        TaskQueue queue1(test_dir);
        for (int i = 0; i < 10; ++i) {
            queue1.submit(createRequest());
        }
        queue1.save();
    }
    
    {
        TaskQueue queue2(test_dir);
        queue2.load();
        
        // Next ID should continue from where we left off
        uint64_t new_id = queue2.submit(createRequest());
        EXPECT_EQ(new_id, 11);
    }
    
    // Cleanup
    std::remove((test_dir + "/tasks.json").c_str());
    rmdir(test_dir.c_str());
}

// Test persistence with completed tasks
TEST_F(TaskQueueTest, PersistenceWithCompletedTasks) {
    std::string test_dir = "/tmp/myqueue_completed_" + std::to_string(getpid());
    
    {
        TaskQueue queue1(test_dir);
        uint64_t id = queue1.submit(createRequest());
        queue1.setTaskRunning(id, 12345, {0}, {0});
        queue1.setTaskCompleted(id, 0);
        queue1.save();
    }
    
    {
        TaskQueue queue2(test_dir);
        queue2.load();
        
        auto task = queue2.getTask(1);
        ASSERT_TRUE(task.has_value());
        EXPECT_EQ(task->status, TaskStatus::COMPLETED);
        EXPECT_EQ(task->exit_code, 0);
        EXPECT_TRUE(task->end_time.has_value());
    }
    
    // Cleanup
    std::remove((test_dir + "/tasks.json").c_str());
    rmdir(test_dir.c_str());
}

// Test task state transitions
TEST_F(TaskQueueTest, TaskStateTransitions) {
    uint64_t id = queue_->submit(createRequest());
    
    // PENDING -> RUNNING
    auto task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::PENDING);
    EXPECT_TRUE(task->canSchedule());
    EXPECT_FALSE(task->isTerminal());
    
    queue_->setTaskRunning(id, 12345, {0}, {0});
    task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::RUNNING);
    EXPECT_FALSE(task->canSchedule());
    EXPECT_FALSE(task->isTerminal());
    
    // RUNNING -> COMPLETED
    queue_->setTaskCompleted(id, 0);
    task = queue_->getTask(id);
    EXPECT_EQ(task->status, TaskStatus::COMPLETED);
    EXPECT_FALSE(task->canSchedule());
    EXPECT_TRUE(task->isTerminal());
}

// Test thread safety - concurrent submissions
TEST_F(TaskQueueTest, ConcurrentSubmissions) {
    const int num_threads = 10;
    const int submissions_per_thread = 100;
    std::vector<std::thread> threads;
    std::vector<std::set<uint64_t>> thread_ids(num_threads);
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, &thread_ids, submissions_per_thread]() {
            for (int i = 0; i < submissions_per_thread; ++i) {
                auto req = createRequest("script_" + std::to_string(t) + "_" + std::to_string(i) + ".sh");
                uint64_t id = queue_->submit(req);
                thread_ids[t].insert(id);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Check total count
    EXPECT_EQ(queue_->size(), num_threads * submissions_per_thread);
    
    // Check all IDs are unique across all threads
    std::set<uint64_t> all_ids;
    for (const auto& ids : thread_ids) {
        for (uint64_t id : ids) {
            EXPECT_EQ(all_ids.count(id), 0) << "Duplicate ID found: " << id;
            all_ids.insert(id);
        }
    }
    EXPECT_EQ(all_ids.size(), num_threads * submissions_per_thread);
}

} // namespace testing
} // namespace myqueue

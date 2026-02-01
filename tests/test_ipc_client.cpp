/**
 * @file test_ipc_client.cpp
 * @brief Unit tests for IPCClient class
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <filesystem>

#include "myqueue/ipc_client.h"
#include "myqueue/ipc_server.h"
#include "myqueue/protocol.h"
#include "myqueue/errors.h"

namespace myqueue {
namespace {

// Test fixture for IPCClient tests
class IPCClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique socket path for each test
        socket_path_ = "/tmp/myqueue_test_client_" + 
                       std::to_string(getpid()) + "_" +
                       std::to_string(test_counter_++) + ".sock";
        
        // Remove socket file if it exists
        std::filesystem::remove(socket_path_);
    }
    
    void TearDown() override {
        // Clean up socket file
        std::filesystem::remove(socket_path_);
    }
    
    std::string socket_path_;
    static int test_counter_;
};

int IPCClientTest::test_counter_ = 0;

// Test: Client construction
TEST_F(IPCClientTest, Construction) {
    IPCClient client(socket_path_);
    EXPECT_EQ(client.socketPath(), socket_path_);
    EXPECT_FALSE(client.isConnected());
}

// Test: Connect to non-existent server
TEST_F(IPCClientTest, ConnectToNonExistentServer) {
    IPCClient client(socket_path_);
    EXPECT_FALSE(client.connect());
    EXPECT_FALSE(client.isConnected());
    EXPECT_FALSE(client.lastError().empty());
}

// Test: Connect and disconnect
TEST_F(IPCClientTest, ConnectAndDisconnect) {
    // Start server
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) {
        return "{}";
    });
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect client
    IPCClient client(socket_path_);
    EXPECT_TRUE(client.connect());
    EXPECT_TRUE(client.isConnected());
    
    // Disconnect
    client.disconnect();
    EXPECT_FALSE(client.isConnected());
    
    // Stop server
    server.stop();
}

// Test: Submit request
TEST_F(IPCClientTest, SubmitRequest) {
    uint64_t expected_id = 42;
    
    // Start server with handler
    IPCServer server(socket_path_);
    server.start([expected_id](MsgType type, const std::string& payload) {
        if (type == MsgType::SUBMIT) {
            SubmitResponse resp;
            resp.task_id = expected_id;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect and submit
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/home/user/work";
    req.ncpu = 2;
    req.ngpu = 1;
    
    auto result = client.submit(req);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, expected_id);
    
    client.disconnect();
    server.stop();
}

// Test: Query queue
TEST_F(IPCClientTest, QueryQueue) {
    // Start server with handler
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) {
        if (type == MsgType::QUERY_QUEUE) {
            QueueResponse resp;
            
            TaskInfo running_task;
            running_task.id = 1;
            running_task.status = "running";
            running_task.script = "/path/to/script1.sh";
            running_task.workdir = "/work1";
            running_task.cpus = {0, 1};
            running_task.gpus = {0};
            resp.running.push_back(running_task);
            
            TaskInfo pending_task;
            pending_task.id = 2;
            pending_task.status = "pending";
            pending_task.script = "/path/to/script2.sh";
            pending_task.workdir = "/work2";
            resp.pending.push_back(pending_task);
            
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect and query
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    auto result = client.queryQueue();
    ASSERT_TRUE(result.has_value());
    
    EXPECT_EQ(result->running.size(), 1);
    EXPECT_EQ(result->pending.size(), 1);
    
    EXPECT_EQ(result->running[0].id, 1);
    EXPECT_EQ(result->running[0].status, "running");
    EXPECT_EQ(result->running[0].cpus.size(), 2);
    EXPECT_EQ(result->running[0].gpus.size(), 1);
    
    EXPECT_EQ(result->pending[0].id, 2);
    EXPECT_EQ(result->pending[0].status, "pending");
    
    client.disconnect();
    server.stop();
}

// Test: Delete tasks
TEST_F(IPCClientTest, DeleteTasks) {
    // Start server with handler
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) {
        if (type == MsgType::DELETE_TASK) {
            DeleteRequest req = DeleteRequest::fromJson(payload);
            DeleteResponse resp;
            // Simulate: first task deleted, second not found
            for (size_t i = 0; i < req.task_ids.size(); ++i) {
                resp.results.push_back(req.task_ids[i] == 1);
            }
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect and delete
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    std::vector<uint64_t> ids = {1, 2, 3};
    auto results = client.deleteTasks(ids);
    
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results[0]);   // Task 1 deleted
    EXPECT_FALSE(results[1]);  // Task 2 not found
    EXPECT_FALSE(results[2]);  // Task 3 not found
    
    client.disconnect();
    server.stop();
}

// Test: Delete empty list
TEST_F(IPCClientTest, DeleteEmptyList) {
    IPCClient client(socket_path_);
    
    // Start server
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) {
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    ASSERT_TRUE(client.connect());
    
    std::vector<uint64_t> empty_ids;
    auto results = client.deleteTasks(empty_ids);
    EXPECT_TRUE(results.empty());
    
    client.disconnect();
    server.stop();
}

// Test: Server error response
TEST_F(IPCClientTest, ServerErrorResponse) {
    // Start server that returns error
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) -> std::string {
        throw MyQueueException(ErrorCode::TASK_NOT_FOUND, "Task does not exist");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect and submit
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/home/user/work";
    
    auto result = client.submit(req);
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(client.lastError().empty());
    
    client.disconnect();
    server.stop();
}

// Test: Move semantics
TEST_F(IPCClientTest, MoveSemantics) {
    IPCClient client1(socket_path_);
    
    // Move construct
    IPCClient client2(std::move(client1));
    EXPECT_EQ(client2.socketPath(), socket_path_);
    
    // Move assign
    std::string other_path = "/tmp/other.sock";
    IPCClient client3(other_path);
    client3 = std::move(client2);
    EXPECT_EQ(client3.socketPath(), socket_path_);
}

// Test: Multiple requests on same connection
TEST_F(IPCClientTest, MultipleRequests) {
    uint64_t task_counter = 0;
    
    // Start server
    IPCServer server(socket_path_);
    server.start([&task_counter](MsgType type, const std::string& payload) {
        if (type == MsgType::SUBMIT) {
            SubmitResponse resp;
            resp.task_id = ++task_counter;
            return resp.toJson();
        } else if (type == MsgType::QUERY_QUEUE) {
            QueueResponse resp;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Connect and send multiple requests
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/home/user/work";
    
    // Submit multiple tasks
    auto id1 = client.submit(req);
    auto id2 = client.submit(req);
    auto id3 = client.submit(req);
    
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    ASSERT_TRUE(id3.has_value());
    
    EXPECT_EQ(*id1, 1);
    EXPECT_EQ(*id2, 2);
    EXPECT_EQ(*id3, 3);
    
    // Query queue
    auto queue = client.queryQueue();
    EXPECT_TRUE(queue.has_value());
    
    client.disconnect();
    server.stop();
}

// Test: Reconnect after disconnect
TEST_F(IPCClientTest, Reconnect) {
    // Start server
    IPCServer server(socket_path_);
    server.start([](MsgType type, const std::string& payload) {
        if (type == MsgType::QUERY_QUEUE) {
            QueueResponse resp;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    IPCClient client(socket_path_);
    
    // First connection
    ASSERT_TRUE(client.connect());
    auto result1 = client.queryQueue();
    EXPECT_TRUE(result1.has_value());
    client.disconnect();
    
    // Reconnect
    ASSERT_TRUE(client.connect());
    auto result2 = client.queryQueue();
    EXPECT_TRUE(result2.has_value());
    client.disconnect();
    
    server.stop();
}

// Test: Operations without connection
TEST_F(IPCClientTest, OperationsWithoutConnection) {
    IPCClient client(socket_path_);
    
    // Try operations without connecting
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/home/user/work";
    
    auto submit_result = client.submit(req);
    EXPECT_FALSE(submit_result.has_value());
    
    auto query_result = client.queryQueue();
    EXPECT_FALSE(query_result.has_value());
    
    auto delete_result = client.deleteTasks({1, 2, 3});
    EXPECT_TRUE(delete_result.empty());
}

// Test: Shutdown request
TEST_F(IPCClientTest, ShutdownRequest) {
    bool shutdown_received = false;
    
    // Start server
    IPCServer server(socket_path_);
    server.start([&shutdown_received](MsgType type, const std::string& payload) {
        if (type == MsgType::SHUTDOWN) {
            shutdown_received = true;
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    IPCClient client(socket_path_);
    ASSERT_TRUE(client.connect());
    
    bool result = client.shutdown();
    EXPECT_TRUE(result);
    EXPECT_TRUE(shutdown_received);
    
    client.disconnect();
    server.stop();
}

} // namespace
} // namespace myqueue

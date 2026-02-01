/**
 * @file test_ipc_server.cpp
 * @brief Unit tests for IPCServer class
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

#include "myqueue/ipc_server.h"
#include "myqueue/protocol.h"
#include "myqueue/errors.h"
#include "json.hpp"

using namespace myqueue;
using json = nlohmann::json;

namespace {

// Helper function to create a client connection
int connectToServer(const std::string& socket_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

// Helper function to send a message
bool sendMessage(int fd, MsgType type, const std::string& payload) {
    json j;
    j["type"] = msgTypeToString(type);
    try {
        j["payload"] = json::parse(payload);
    } catch (...) {
        j["payload"] = payload;
    }
    
    std::string message = j.dump();
    uint32_t length = static_cast<uint32_t>(message.size());
    uint32_t length_net = htonl(length);
    
    if (write(fd, &length_net, sizeof(length_net)) != sizeof(length_net)) {
        return false;
    }
    
    if (write(fd, message.data(), message.size()) != static_cast<ssize_t>(message.size())) {
        return false;
    }
    
    return true;
}

// Helper function to receive a message
bool receiveMessage(int fd, MsgType& type, std::string& payload) {
    uint32_t length_net;
    if (read(fd, &length_net, sizeof(length_net)) != sizeof(length_net)) {
        return false;
    }
    
    uint32_t length = ntohl(length_net);
    if (length == 0 || length > 16 * 1024 * 1024) {
        return false;
    }
    
    std::string message(length, '\0');
    size_t total_read = 0;
    while (total_read < length) {
        ssize_t n = read(fd, &message[total_read], length - total_read);
        if (n <= 0) {
            return false;
        }
        total_read += n;
    }
    
    try {
        json j = json::parse(message);
        type = msgTypeFromString(j["type"].get<std::string>());
        if (j.contains("payload")) {
            if (j["payload"].is_string()) {
                payload = j["payload"].get<std::string>();
            } else {
                payload = j["payload"].dump();
            }
        } else {
            payload = "{}";
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Generate unique socket path for each test
std::string getTestSocketPath() {
    static int counter = 0;
    return "/tmp/myqueue_test_" + std::to_string(getpid()) + "_" + 
           std::to_string(counter++) + ".sock";
}

} // anonymous namespace

class IPCServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        socket_path_ = getTestSocketPath();
    }
    
    void TearDown() override {
        unlink(socket_path_.c_str());
    }
    
    std::string socket_path_;
};

// Test: Server can start and stop
TEST_F(IPCServerTest, StartAndStop) {
    IPCServer server(socket_path_);
    
    EXPECT_FALSE(server.isRunning());
    
    server.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    EXPECT_TRUE(server.isRunning());
    EXPECT_EQ(server.socketPath(), socket_path_);
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    server.stop();
    
    EXPECT_FALSE(server.isRunning());
}

// Test: Server creates socket file
TEST_F(IPCServerTest, CreatesSocketFile) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Check socket file exists
    EXPECT_EQ(access(socket_path_.c_str(), F_OK), 0);
    
    server.stop();
    
    // Socket file should be removed after stop
    EXPECT_NE(access(socket_path_.c_str(), F_OK), 0);
}

// Test: Client can connect to server
TEST_F(IPCServerTest, ClientCanConnect) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    EXPECT_GE(client_fd, 0);
    
    if (client_fd >= 0) {
        close(client_fd);
    }
    
    server.stop();
}

// Test: Server handles QUERY_QUEUE request
TEST_F(IPCServerTest, HandleQueryQueueRequest) {
    IPCServer server(socket_path_);
    
    QueueResponse expected_response;
    expected_response.running = {};
    expected_response.pending = {};
    
    server.start([&expected_response](MsgType type, const std::string&) {
        if (type == MsgType::QUERY_QUEUE) {
            return expected_response.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    ASSERT_GE(client_fd, 0);
    
    // Send QUERY_QUEUE request
    ASSERT_TRUE(sendMessage(client_fd, MsgType::QUERY_QUEUE, "{}"));
    
    // Receive response
    MsgType response_type;
    std::string response_payload;
    ASSERT_TRUE(receiveMessage(client_fd, response_type, response_payload));
    
    EXPECT_EQ(response_type, MsgType::OK);
    
    QueueResponse actual_response = QueueResponse::fromJson(response_payload);
    EXPECT_EQ(actual_response, expected_response);
    
    close(client_fd);
    server.stop();
}

// Test: Server handles SUBMIT request
TEST_F(IPCServerTest, HandleSubmitRequest) {
    IPCServer server(socket_path_);
    
    SubmitRequest received_request;
    bool request_received = false;
    
    server.start([&received_request, &request_received](MsgType type, const std::string& payload) {
        if (type == MsgType::SUBMIT) {
            received_request = SubmitRequest::fromJson(payload);
            request_received = true;
            SubmitResponse resp;
            resp.task_id = 42;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    ASSERT_GE(client_fd, 0);
    
    // Create submit request
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/home/user/work";
    req.ncpu = 4;
    req.ngpu = 2;
    req.specific_cpus = {0, 1, 2, 3};
    req.specific_gpus = {0, 1};
    
    // Send SUBMIT request
    ASSERT_TRUE(sendMessage(client_fd, MsgType::SUBMIT, req.toJson()));
    
    // Receive response
    MsgType response_type;
    std::string response_payload;
    ASSERT_TRUE(receiveMessage(client_fd, response_type, response_payload));
    
    EXPECT_EQ(response_type, MsgType::OK);
    EXPECT_TRUE(request_received);
    EXPECT_EQ(received_request, req);
    
    SubmitResponse resp = SubmitResponse::fromJson(response_payload);
    EXPECT_EQ(resp.task_id, 42u);
    
    close(client_fd);
    server.stop();
}

// Test: Server handles DELETE_TASK request
TEST_F(IPCServerTest, HandleDeleteRequest) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType type, const std::string& payload) {
        if (type == MsgType::DELETE_TASK) {
            DeleteRequest req = DeleteRequest::fromJson(payload);
            DeleteResponse resp;
            resp.results.resize(req.task_ids.size(), true);
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    ASSERT_GE(client_fd, 0);
    
    // Create delete request
    DeleteRequest req;
    req.task_ids = {1, 2, 3};
    
    // Send DELETE_TASK request
    ASSERT_TRUE(sendMessage(client_fd, MsgType::DELETE_TASK, req.toJson()));
    
    // Receive response
    MsgType response_type;
    std::string response_payload;
    ASSERT_TRUE(receiveMessage(client_fd, response_type, response_payload));
    
    EXPECT_EQ(response_type, MsgType::OK);
    
    DeleteResponse resp = DeleteResponse::fromJson(response_payload);
    EXPECT_EQ(resp.results.size(), 3u);
    EXPECT_TRUE(resp.results[0]);
    EXPECT_TRUE(resp.results[1]);
    EXPECT_TRUE(resp.results[2]);
    
    close(client_fd);
    server.stop();
}

// Test: Server handles multiple clients
TEST_F(IPCServerTest, HandleMultipleClients) {
    IPCServer server(socket_path_);
    
    std::atomic<int> request_count{0};
    
    server.start([&request_count](MsgType type, const std::string&) {
        if (type == MsgType::QUERY_QUEUE) {
            request_count++;
            QueueResponse resp;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    const int num_clients = 5;
    std::vector<std::thread> client_threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([this, &success_count]() {
            int client_fd = connectToServer(socket_path_);
            if (client_fd < 0) {
                return;
            }
            
            if (sendMessage(client_fd, MsgType::QUERY_QUEUE, "{}")) {
                MsgType response_type;
                std::string response_payload;
                if (receiveMessage(client_fd, response_type, response_payload)) {
                    if (response_type == MsgType::OK) {
                        success_count++;
                    }
                }
            }
            
            close(client_fd);
        });
    }
    
    for (auto& t : client_threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), num_clients);
    EXPECT_EQ(request_count.load(), num_clients);
    
    server.stop();
}

// Test: Server handles exception in handler
TEST_F(IPCServerTest, HandleExceptionInHandler) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType, const std::string&) -> std::string {
        throw MyQueueException(ErrorCode::TASK_NOT_FOUND, "Test error");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    ASSERT_GE(client_fd, 0);
    
    // Send request
    ASSERT_TRUE(sendMessage(client_fd, MsgType::QUERY_QUEUE, "{}"));
    
    // Receive error response
    MsgType response_type;
    std::string response_payload;
    ASSERT_TRUE(receiveMessage(client_fd, response_type, response_payload));
    
    EXPECT_EQ(response_type, MsgType::ERROR);
    
    ErrorResponse err = ErrorResponse::fromJson(response_payload);
    EXPECT_EQ(err.code, static_cast<int>(ErrorCode::TASK_NOT_FOUND));
    EXPECT_EQ(err.message, "Test error");
    
    close(client_fd);
    server.stop();
}

// Test: Server handles multiple requests on same connection
TEST_F(IPCServerTest, HandleMultipleRequestsOnSameConnection) {
    IPCServer server(socket_path_);
    
    std::atomic<int> request_count{0};
    
    server.start([&request_count](MsgType type, const std::string&) {
        request_count++;
        if (type == MsgType::QUERY_QUEUE) {
            QueueResponse resp;
            return resp.toJson();
        }
        return std::string("{}");
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    int client_fd = connectToServer(socket_path_);
    ASSERT_GE(client_fd, 0);
    
    // Send multiple requests on same connection
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(sendMessage(client_fd, MsgType::QUERY_QUEUE, "{}"));
        
        MsgType response_type;
        std::string response_payload;
        ASSERT_TRUE(receiveMessage(client_fd, response_type, response_payload));
        EXPECT_EQ(response_type, MsgType::OK);
    }
    
    EXPECT_EQ(request_count.load(), 3);
    
    close(client_fd);
    server.stop();
}

// Test: Server can be moved
TEST_F(IPCServerTest, MoveSemantics) {
    IPCServer server1(socket_path_);
    
    server1.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(server1.isRunning());
    
    // Move construct
    IPCServer server2(std::move(server1));
    
    EXPECT_FALSE(server1.isRunning());
    EXPECT_TRUE(server2.isRunning());
    
    // Client should still be able to connect
    int client_fd = connectToServer(socket_path_);
    EXPECT_GE(client_fd, 0);
    
    if (client_fd >= 0) {
        close(client_fd);
    }
    
    server2.stop();
}

// Test: Double start is safe
TEST_F(IPCServerTest, DoubleStartIsSafe) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Second start should be no-op
    EXPECT_NO_THROW(server.start([](MsgType, const std::string&) {
        return "{}";
    }));
    
    EXPECT_TRUE(server.isRunning());
    
    server.stop();
}

// Test: Double stop is safe
TEST_F(IPCServerTest, DoubleStopIsSafe) {
    IPCServer server(socket_path_);
    
    server.start([](MsgType, const std::string&) {
        return "{}";
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    server.stop();
    EXPECT_FALSE(server.isRunning());
    
    // Second stop should be no-op
    EXPECT_NO_THROW(server.stop());
    EXPECT_FALSE(server.isRunning());
}

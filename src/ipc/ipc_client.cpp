/**
 * @file ipc_client.cpp
 * @brief Implementation of Unix Domain Socket client
 */

#include "myqueue/ipc_client.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>  // for htonl, ntohl

namespace myqueue {

using json = nlohmann::json;

// Maximum message size (16 MB) - same as server
constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024;

// Connection timeout in seconds
constexpr int CONNECT_TIMEOUT_SEC = 5;

// Read/Write timeout in seconds
constexpr int IO_TIMEOUT_SEC = 30;

IPCClient::IPCClient(const std::string& socket_path)
    : socket_path_(socket_path) {
}

IPCClient::~IPCClient() {
    disconnect();
}

IPCClient::IPCClient(IPCClient&& other) noexcept
    : socket_path_(std::move(other.socket_path_))
    , fd_(other.fd_)
    , last_error_(std::move(other.last_error_)) {
    other.fd_ = -1;
}

IPCClient& IPCClient::operator=(IPCClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        socket_path_ = std::move(other.socket_path_);
        fd_ = other.fd_;
        last_error_ = std::move(other.last_error_);
        other.fd_ = -1;
    }
    return *this;
}

bool IPCClient::connect() {
    if (fd_ >= 0) {
        return true;  // Already connected
    }
    
    last_error_.clear();
    
    // Create socket
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        last_error_ = "Failed to create socket: " + std::string(strerror(errno));
        return false;
    }
    
    // Set socket timeouts
    struct timeval tv;
    tv.tv_sec = IO_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    // Prepare address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    if (socket_path_.length() >= sizeof(addr.sun_path)) {
        close(fd_);
        fd_ = -1;
        last_error_ = "Socket path too long";
        return false;
    }
    
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    // Connect to server
    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        close(fd_);
        fd_ = -1;
        
        if (err == ENOENT || err == ECONNREFUSED) {
            last_error_ = "Server is not running";
        } else {
            last_error_ = "Failed to connect: " + std::string(strerror(err));
        }
        return false;
    }
    
    return true;
}

void IPCClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

std::optional<uint64_t> IPCClient::submit(const SubmitRequest& req) {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return std::nullopt;
    }
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::SUBMIT, req.toJson(), response_type, response_payload)) {
        return std::nullopt;
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return std::nullopt;
    }
    
    try {
        SubmitResponse resp = SubmitResponse::fromJson(response_payload);
        return resp.task_id;
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return std::nullopt;
    }
}

std::optional<QueueResponse> IPCClient::queryQueue(bool include_completed) {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return std::nullopt;
    }
    
    MsgType response_type;
    std::string response_payload;
    
    MsgType request_type = include_completed ? MsgType::QUERY_QUEUE_ALL : MsgType::QUERY_QUEUE;
    
    if (!sendRequest(request_type, "{}", response_type, response_payload)) {
        return std::nullopt;
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return std::nullopt;
    }
    
    try {
        return QueueResponse::fromJson(response_payload);
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return std::nullopt;
    }
}

std::vector<bool> IPCClient::deleteTasks(const std::vector<uint64_t>& ids) {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return {};
    }
    
    if (ids.empty()) {
        return {};
    }
    
    DeleteRequest req;
    req.task_ids = ids;
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::DELETE_TASK, req.toJson(), response_type, response_payload)) {
        return {};
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return {};
    }
    
    try {
        DeleteResponse resp = DeleteResponse::fromJson(response_payload);
        return resp.results;
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return {};
    }
}

std::optional<DeleteAllResponse> IPCClient::deleteAll() {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return std::nullopt;
    }
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::DELETE_ALL, "{}", response_type, response_payload)) {
        return std::nullopt;
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return std::nullopt;
    }
    
    try {
        return DeleteAllResponse::fromJson(response_payload);
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return std::nullopt;
    }
}

std::optional<TaskDetailResponse> IPCClient::getTaskInfo(uint64_t task_id) {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return std::nullopt;
    }
    
    TaskInfoRequest req;
    req.task_id = task_id;
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::GET_TASK_INFO, req.toJson(), response_type, response_payload)) {
        return std::nullopt;
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return std::nullopt;
    }
    
    try {
        return TaskDetailResponse::fromJson(response_payload);
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return std::nullopt;
    }
}

std::optional<TaskLogResponse> IPCClient::getTaskLog(uint64_t task_id, int tail_lines) {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return std::nullopt;
    }
    
    TaskLogRequest req;
    req.task_id = task_id;
    req.tail_lines = tail_lines;
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::GET_TASK_LOG, req.toJson(), response_type, response_payload)) {
        return std::nullopt;
    }
    
    if (response_type == MsgType::ERROR) {
        try {
            ErrorResponse err = ErrorResponse::fromJson(response_payload);
            last_error_ = err.message;
        } catch (...) {
            last_error_ = "Server returned error";
        }
        return std::nullopt;
    }
    
    try {
        return TaskLogResponse::fromJson(response_payload);
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse response: " + std::string(e.what());
        return std::nullopt;
    }
}

bool IPCClient::shutdown() {
    if (!isConnected()) {
        last_error_ = "Not connected to server";
        return false;
    }
    
    MsgType response_type;
    std::string response_payload;
    
    if (!sendRequest(MsgType::SHUTDOWN, "{}", response_type, response_payload)) {
        return false;
    }
    
    return response_type == MsgType::OK;
}

bool IPCClient::sendRequest(MsgType type, const std::string& payload,
                            MsgType& response_type, std::string& response_payload) {
    // Send request
    if (!writeMessage(type, payload)) {
        return false;
    }
    
    // Read response
    if (!readMessage(response_type, response_payload)) {
        return false;
    }
    
    return true;
}

bool IPCClient::writeMessage(MsgType type, const std::string& payload) {
    // Build JSON message
    json j;
    j["type"] = msgTypeToString(type);
    
    // Try to parse payload as JSON, otherwise use as string
    try {
        j["payload"] = json::parse(payload);
    } catch (...) {
        j["payload"] = payload;
    }
    
    std::string message = j.dump();
    
    // Write 4-byte length header (network byte order)
    uint32_t length = static_cast<uint32_t>(message.size());
    uint32_t length_net = htonl(length);
    
    if (!writeExact(&length_net, sizeof(length_net))) {
        last_error_ = "Failed to send message header";
        return false;
    }
    
    // Write message body
    if (!writeExact(message.data(), message.size())) {
        last_error_ = "Failed to send message body";
        return false;
    }
    
    return true;
}

bool IPCClient::readMessage(MsgType& type, std::string& payload) {
    // Read 4-byte length header
    uint32_t length_net;
    if (!readExact(&length_net, sizeof(length_net))) {
        last_error_ = "Failed to read message header";
        return false;
    }
    
    uint32_t length = ntohl(length_net);
    
    // Validate length
    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        last_error_ = "Invalid message length";
        return false;
    }
    
    // Read message body
    std::string message(length, '\0');
    if (!readExact(&message[0], length)) {
        last_error_ = "Failed to read message body";
        return false;
    }
    
    // Parse JSON
    try {
        json j = json::parse(message);
        
        if (!j.contains("type")) {
            last_error_ = "Message missing type field";
            return false;
        }
        
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
    } catch (const std::exception& e) {
        last_error_ = "Failed to parse message: " + std::string(e.what());
        return false;
    }
}

bool IPCClient::readExact(void* buffer, size_t n) {
    char* buf = static_cast<char*>(buffer);
    size_t total_read = 0;
    
    while (total_read < n) {
        ssize_t bytes_read = read(fd_, buf + total_read, n - total_read);
        
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, retry
            }
            return false;  // Error
        }
        
        if (bytes_read == 0) {
            return false;  // Connection closed
        }
        
        total_read += bytes_read;
    }
    
    return true;
}

bool IPCClient::writeExact(const void* buffer, size_t n) {
    const char* buf = static_cast<const char*>(buffer);
    size_t total_written = 0;
    
    while (total_written < n) {
        ssize_t bytes_written = write(fd_, buf + total_written, n - total_written);
        
        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, retry
            }
            return false;  // Error
        }
        
        total_written += bytes_written;
    }
    
    return true;
}

} // namespace myqueue

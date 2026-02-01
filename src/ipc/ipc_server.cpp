/**
 * @file ipc_server.cpp
 * @brief Implementation of Unix Domain Socket server
 */

#include "myqueue/ipc_server.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>  // for htonl, ntohl

namespace myqueue {

using json = nlohmann::json;

// Maximum message size (16 MB)
constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024;

// Connection backlog
constexpr int LISTEN_BACKLOG = 10;

// Poll timeout in milliseconds
constexpr int POLL_TIMEOUT_MS = 100;

IPCServer::IPCServer(const std::string& socket_path)
    : socket_path_(socket_path) {
}

IPCServer::~IPCServer() {
    stop();
}

IPCServer::IPCServer(IPCServer&& other) noexcept
    : socket_path_(std::move(other.socket_path_))
    , server_fd_(other.server_fd_)
    , running_(other.running_.load())
    , accept_thread_(std::move(other.accept_thread_))
    , handler_(std::move(other.handler_)) {
    other.server_fd_ = -1;
    other.running_ = false;
}

IPCServer& IPCServer::operator=(IPCServer&& other) noexcept {
    if (this != &other) {
        stop();
        socket_path_ = std::move(other.socket_path_);
        server_fd_ = other.server_fd_;
        running_ = other.running_.load();
        accept_thread_ = std::move(other.accept_thread_);
        handler_ = std::move(other.handler_);
        other.server_fd_ = -1;
        other.running_ = false;
    }
    return *this;
}

void IPCServer::start(RequestHandler handler) {
    if (running_.load()) {
        return;  // Already running
    }
    
    handler_ = std::move(handler);
    
    // Remove existing socket file if present
    unlink(socket_path_.c_str());
    
    // Create socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw MyQueueException(ErrorCode::IPC_CONNECTION_FAILED,
            "Failed to create socket: " + std::string(strerror(errno)));
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to socket path
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    if (socket_path_.length() >= sizeof(addr.sun_path)) {
        close(server_fd_);
        server_fd_ = -1;
        throw MyQueueException(ErrorCode::IPC_CONNECTION_FAILED,
            "Socket path too long");
    }
    
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        throw MyQueueException(ErrorCode::IPC_CONNECTION_FAILED,
            "Failed to bind socket: " + std::string(strerror(errno)));
    }
    
    // Listen for connections
    if (listen(server_fd_, LISTEN_BACKLOG) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        throw MyQueueException(ErrorCode::IPC_CONNECTION_FAILED,
            "Failed to listen on socket: " + std::string(strerror(errno)));
    }
    
    // Start accept thread
    running_ = true;
    accept_thread_ = std::thread(&IPCServer::acceptLoop, this);
}

void IPCServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    // Close server socket to interrupt accept()
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // Wait for accept thread to finish
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // Wait for all client threads to finish
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        client_threads_.clear();
    }
    
    // Remove socket file
    unlink(socket_path_.c_str());
}

void IPCServer::acceptLoop() {
    while (running_.load()) {
        // Use poll to allow periodic checking of running_ flag
        struct pollfd pfd;
        pfd.fd = server_fd_;
        pfd.events = POLLIN;
        
        int ret = poll(&pfd, 1, POLL_TIMEOUT_MS);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, check running_ and retry
            }
            break;  // Error
        }
        
        if (ret == 0) {
            continue;  // Timeout, check running_ and retry
        }
        
        if (!(pfd.revents & POLLIN)) {
            continue;
        }
        
        // Accept new connection
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, 
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &client_len);
        
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (!running_.load()) {
                break;  // Server is stopping
            }
            continue;  // Other error, try again
        }
        
        // Handle client in a new thread
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            
            // Clean up finished threads
            client_threads_.erase(
                std::remove_if(client_threads_.begin(), client_threads_.end(),
                    [](std::thread& t) {
                        if (t.joinable()) {
                            // Check if thread is done (non-blocking)
                            // Since we can't do this easily, we'll just keep all threads
                            // and clean up on stop()
                            return false;
                        }
                        return true;
                    }),
                client_threads_.end()
            );
            
            // Start new client thread
            client_threads_.emplace_back(&IPCServer::handleClient, this, client_fd);
        }
    }
}

void IPCServer::handleClient(int client_fd) {
    // Set socket timeout for reads
    struct timeval tv;
    tv.tv_sec = 30;  // 30 second timeout
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    while (running_.load()) {
        MsgType type;
        std::string payload;
        
        // Read request
        if (!readMessage(client_fd, type, payload)) {
            break;  // Connection closed or error
        }
        
        // Process request
        std::string response;
        MsgType response_type = MsgType::OK;
        
        try {
            if (handler_) {
                response = handler_(type, payload);
            } else {
                response_type = MsgType::ERROR;
                ErrorResponse err;
                err.code = static_cast<int>(ErrorCode::IPC_PROTOCOL_ERROR);
                err.message = "No handler registered";
                response = err.toJson();
            }
        } catch (const MyQueueException& e) {
            response_type = MsgType::ERROR;
            ErrorResponse err;
            err.code = static_cast<int>(e.code());
            err.message = e.message();
            response = err.toJson();
        } catch (const std::exception& e) {
            response_type = MsgType::ERROR;
            ErrorResponse err;
            err.code = static_cast<int>(ErrorCode::IPC_PROTOCOL_ERROR);
            err.message = e.what();
            response = err.toJson();
        }
        
        // Send response
        if (!writeMessage(client_fd, response_type, response)) {
            break;  // Connection closed or error
        }
        
        // Handle shutdown request
        if (type == MsgType::SHUTDOWN) {
            break;
        }
    }
    
    close(client_fd);
}

bool IPCServer::readMessage(int fd, MsgType& type, std::string& payload) {
    // Read 4-byte length header
    uint32_t length_net;
    if (!readExact(fd, &length_net, sizeof(length_net))) {
        return false;
    }
    
    uint32_t length = ntohl(length_net);
    
    // Validate length
    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        return false;
    }
    
    // Read message body
    std::string message(length, '\0');
    if (!readExact(fd, &message[0], length)) {
        return false;
    }
    
    // Parse JSON
    try {
        json j = json::parse(message);
        
        if (!j.contains("type")) {
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
    } catch (...) {
        return false;
    }
}

bool IPCServer::writeMessage(int fd, MsgType type, const std::string& payload) {
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
    
    // Write 4-byte length header
    uint32_t length = static_cast<uint32_t>(message.size());
    uint32_t length_net = htonl(length);
    
    if (!writeExact(fd, &length_net, sizeof(length_net))) {
        return false;
    }
    
    // Write message body
    return writeExact(fd, message.data(), message.size());
}

bool IPCServer::readExact(int fd, void* buffer, size_t n) {
    char* buf = static_cast<char*>(buffer);
    size_t total_read = 0;
    
    while (total_read < n) {
        ssize_t bytes_read = read(fd, buf + total_read, n - total_read);
        
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

bool IPCServer::writeExact(int fd, const void* buffer, size_t n) {
    const char* buf = static_cast<const char*>(buffer);
    size_t total_written = 0;
    
    while (total_written < n) {
        ssize_t bytes_written = write(fd, buf + total_written, n - total_written);
        
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

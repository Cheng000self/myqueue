/**
 * @file ipc_server.h
 * @brief Unix Domain Socket server for IPC communication
 * 
 * Implements a server that listens on a Unix Domain Socket and handles
 * incoming requests from CLI clients. The server uses a thread pool
 * to handle multiple concurrent connections.
 */

#ifndef MYQUEUE_IPC_SERVER_H
#define MYQUEUE_IPC_SERVER_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "myqueue/protocol.h"

namespace myqueue {

/**
 * @brief Unix Domain Socket server for handling IPC requests
 * 
 * The IPCServer listens on a Unix Domain Socket and accepts connections
 * from CLI clients. Each connection is handled in a separate thread.
 * 
 * Message format:
 * - 4 bytes: message length (network byte order)
 * - N bytes: JSON message body
 * 
 * The JSON message contains:
 * - "type": message type string (e.g., "SUBMIT", "QUERY_QUEUE")
 * - "payload": message-specific data
 * 
 * Usage:
 * @code
 * IPCServer server("/tmp/myqueue.sock");
 * server.start([](MsgType type, const std::string& payload) {
 *     // Handle request and return response JSON
 *     return response_json;
 * });
 * // ... later
 * server.stop();
 * @endcode
 */
class IPCServer {
public:
    /**
     * @brief Request handler function type
     * 
     * The handler receives the message type and payload, and returns
     * a JSON string response.
     * 
     * @param type The message type
     * @param payload The JSON payload string
     * @return JSON response string
     */
    using RequestHandler = std::function<std::string(MsgType, const std::string&)>;
    
    /**
     * @brief Construct an IPC server
     * @param socket_path Path to the Unix Domain Socket
     */
    explicit IPCServer(const std::string& socket_path);
    
    /**
     * @brief Destructor - stops the server if running
     */
    ~IPCServer();
    
    // Non-copyable
    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;
    
    // Movable
    IPCServer(IPCServer&& other) noexcept;
    IPCServer& operator=(IPCServer&& other) noexcept;
    
    /**
     * @brief Start the server
     * 
     * Creates the Unix Domain Socket, binds to it, and starts
     * accepting connections in a background thread.
     * 
     * @param handler Function to handle incoming requests
     * @throws MyQueueException if server cannot be started
     */
    void start(RequestHandler handler);
    
    /**
     * @brief Stop the server
     * 
     * Stops accepting new connections, closes all existing connections,
     * and cleans up the socket file.
     */
    void stop();
    
    /**
     * @brief Check if server is running
     * @return true if server is running
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Get the socket path
     * @return The Unix Domain Socket path
     */
    const std::string& socketPath() const { return socket_path_; }

private:
    /**
     * @brief Main accept loop running in background thread
     */
    void acceptLoop();
    
    /**
     * @brief Handle a single client connection
     * @param client_fd Client socket file descriptor
     */
    void handleClient(int client_fd);
    
    /**
     * @brief Read a complete message from socket
     * @param fd Socket file descriptor
     * @param[out] type Message type
     * @param[out] payload Message payload
     * @return true if message was read successfully
     */
    bool readMessage(int fd, MsgType& type, std::string& payload);
    
    /**
     * @brief Write a response message to socket
     * @param fd Socket file descriptor
     * @param type Response message type
     * @param payload Response payload
     * @return true if message was written successfully
     */
    bool writeMessage(int fd, MsgType type, const std::string& payload);
    
    /**
     * @brief Read exactly n bytes from socket
     * @param fd Socket file descriptor
     * @param buffer Buffer to read into
     * @param n Number of bytes to read
     * @return true if all bytes were read
     */
    bool readExact(int fd, void* buffer, size_t n);
    
    /**
     * @brief Write exactly n bytes to socket
     * @param fd Socket file descriptor
     * @param buffer Buffer to write from
     * @param n Number of bytes to write
     * @return true if all bytes were written
     */
    bool writeExact(int fd, const void* buffer, size_t n);
    
    std::string socket_path_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    RequestHandler handler_;
    
    // Track active client threads for cleanup
    std::mutex clients_mutex_;
    std::vector<std::thread> client_threads_;
};

} // namespace myqueue

#endif // MYQUEUE_IPC_SERVER_H

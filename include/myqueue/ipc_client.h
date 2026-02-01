/**
 * @file ipc_client.h
 * @brief Unix Domain Socket client for IPC communication
 * 
 * Implements a client that connects to the myqueue server daemon via
 * Unix Domain Socket and sends requests for task management operations.
 */

#ifndef MYQUEUE_IPC_CLIENT_H
#define MYQUEUE_IPC_CLIENT_H

#include <optional>
#include <string>
#include <vector>

#include "myqueue/protocol.h"

namespace myqueue {

/**
 * @brief Unix Domain Socket client for communicating with the server
 * 
 * The IPCClient connects to the myqueue server daemon and provides
 * methods for submitting tasks, querying the queue, and deleting tasks.
 * 
 * Message format (same as IPCServer):
 * - 4 bytes: message length (network byte order)
 * - N bytes: JSON message body
 * 
 * The JSON message contains:
 * - "type": message type string (e.g., "SUBMIT", "QUERY_QUEUE")
 * - "payload": message-specific data
 * 
 * Usage:
 * @code
 * IPCClient client("/tmp/myqueue.sock");
 * if (client.connect()) {
 *     auto task_id = client.submit(request);
 *     if (task_id) {
 *         std::cout << "Task submitted: " << *task_id << std::endl;
 *     }
 *     client.disconnect();
 * }
 * @endcode
 */
class IPCClient {
public:
    /**
     * @brief Construct an IPC client
     * @param socket_path Path to the Unix Domain Socket
     */
    explicit IPCClient(const std::string& socket_path);
    
    /**
     * @brief Destructor - disconnects if connected
     */
    ~IPCClient();
    
    // Non-copyable
    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;
    
    // Movable
    IPCClient(IPCClient&& other) noexcept;
    IPCClient& operator=(IPCClient&& other) noexcept;
    
    /**
     * @brief Connect to the server
     * 
     * Establishes a connection to the Unix Domain Socket.
     * 
     * @return true if connection succeeded, false otherwise
     */
    bool connect();
    
    /**
     * @brief Disconnect from the server
     * 
     * Closes the connection to the server. Safe to call even if
     * not connected.
     */
    void disconnect();
    
    /**
     * @brief Check if connected to the server
     * @return true if connected
     */
    bool isConnected() const { return fd_ >= 0; }
    
    /**
     * @brief Submit a new task to the server
     * 
     * Sends a submit request and waits for the response.
     * 
     * @param req The submit request containing task details
     * @return The assigned task ID on success, std::nullopt on failure
     */
    std::optional<uint64_t> submit(const SubmitRequest& req);
    
    /**
     * @brief Query the current queue status
     * 
     * Retrieves information about all running and pending tasks.
     * 
     * @param include_completed If true, also include completed/failed/cancelled tasks
     * @return QueueResponse on success, std::nullopt on failure
     */
    std::optional<QueueResponse> queryQueue(bool include_completed = false);
    
    /**
     * @brief Delete one or more tasks
     * 
     * Sends a delete request for the specified task IDs.
     * 
     * @param ids List of task IDs to delete
     * @return Vector of results (true = deleted, false = failed) for each ID,
     *         empty vector on communication failure
     */
    std::vector<bool> deleteTasks(const std::vector<uint64_t>& ids);
    
    /**
     * @brief Delete all tasks
     * 
     * Deletes all tasks (running, pending, and completed).
     * 
     * @return DeleteAllResponse on success, std::nullopt on failure
     */
    std::optional<DeleteAllResponse> deleteAll();
    
    /**
     * @brief Get detailed task information
     * 
     * @param task_id Task ID to query
     * @return TaskDetailResponse on success, std::nullopt on failure
     */
    std::optional<TaskDetailResponse> getTaskInfo(uint64_t task_id);
    
    /**
     * @brief Get task log content
     * 
     * @param task_id Task ID to query
     * @param tail_lines Number of lines from end (0 = all)
     * @return TaskLogResponse on success, std::nullopt on failure
     */
    std::optional<TaskLogResponse> getTaskLog(uint64_t task_id, int tail_lines = 0);
    
    /**
     * @brief Request server shutdown
     * 
     * Sends a shutdown request to the server.
     * 
     * @return true if shutdown request was sent successfully
     */
    bool shutdown();
    
    /**
     * @brief Get the socket path
     * @return The Unix Domain Socket path
     */
    const std::string& socketPath() const { return socket_path_; }
    
    /**
     * @brief Get the last error message
     * @return The last error message, empty if no error
     */
    const std::string& lastError() const { return last_error_; }

private:
    /**
     * @brief Send a request and receive response
     * @param type Request message type
     * @param payload Request payload (JSON string)
     * @param[out] response_type Response message type
     * @param[out] response_payload Response payload (JSON string)
     * @return true if request/response succeeded
     */
    bool sendRequest(MsgType type, const std::string& payload,
                     MsgType& response_type, std::string& response_payload);
    
    /**
     * @brief Write a message to the socket
     * @param type Message type
     * @param payload Message payload
     * @return true if message was written successfully
     */
    bool writeMessage(MsgType type, const std::string& payload);
    
    /**
     * @brief Read a message from the socket
     * @param[out] type Message type
     * @param[out] payload Message payload
     * @return true if message was read successfully
     */
    bool readMessage(MsgType& type, std::string& payload);
    
    /**
     * @brief Read exactly n bytes from socket
     * @param buffer Buffer to read into
     * @param n Number of bytes to read
     * @return true if all bytes were read
     */
    bool readExact(void* buffer, size_t n);
    
    /**
     * @brief Write exactly n bytes to socket
     * @param buffer Buffer to write from
     * @param n Number of bytes to write
     * @return true if all bytes were written
     */
    bool writeExact(const void* buffer, size_t n);
    
    std::string socket_path_;
    int fd_ = -1;
    std::string last_error_;
};

} // namespace myqueue

#endif // MYQUEUE_IPC_CLIENT_H

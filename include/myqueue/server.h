/**
 * @file server.h
 * @brief Server class that integrates all myqueue components
 * 
 * The main server daemon that handles IPC requests and manages
 * task scheduling and execution.
 */

#ifndef MYQUEUE_SERVER_H
#define MYQUEUE_SERVER_H

#include "myqueue/config.h"
#include "myqueue/task_queue.h"
#include "myqueue/resource_monitor.h"
#include "myqueue/executor.h"
#include "myqueue/scheduler.h"
#include "myqueue/ipc_server.h"
#include "myqueue/protocol.h"

#include <atomic>
#include <memory>
#include <string>

namespace myqueue {

/**
 * @brief Main server daemon for myqueue
 * 
 * Integrates all components:
 * - TaskQueue for task management
 * - ResourceMonitor for resource tracking
 * - Executor for task execution
 * - Scheduler for task scheduling
 * - IPCServer for client communication
 * 
 * Handles IPC requests:
 * - SUBMIT: Submit new tasks
 * - QUERY_QUEUE: Query task queue status
 * - DELETE_TASK: Delete tasks
 * - SHUTDOWN: Graceful shutdown
 */
class Server {
public:
    /**
     * @brief Construct a Server with configuration
     * @param config Server configuration
     */
    explicit Server(const Config& config);
    
    /**
     * @brief Destructor - stops the server if running
     */
    ~Server();
    
    // Disable copy
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    
    /**
     * @brief Start the server
     * 
     * Starts all components:
     * - Loads task queue from disk
     * - Starts the scheduler
     * - Starts the IPC server
     * 
     * @return true if server started successfully
     */
    bool start();
    
    /**
     * @brief Stop the server
     * 
     * Stops all components:
     * - Stops the IPC server
     * - Stops the scheduler
     * - Saves task queue to disk
     */
    void stop();
    
    /**
     * @brief Check if the server is running
     * @return true if server is active
     */
    bool isRunning() const;
    
    /**
     * @brief Run the server in the foreground
     * 
     * Blocks until the server is stopped (via signal or shutdown request).
     */
    void run();
    
    /**
     * @brief Daemonize the server
     * 
     * Forks and runs in the background. The parent process exits.
     * 
     * @return true if daemonization was successful (in child process)
     */
    bool daemonize();
    
    /**
     * @brief Get the server configuration
     * @return Reference to configuration
     */
    const Config& getConfig() const;
    
    /**
     * @brief Get the task queue
     * @return Reference to TaskQueue
     */
    TaskQueue& getTaskQueue();
    
    /**
     * @brief Get the resource monitor
     * @return Reference to ResourceMonitor
     */
    ResourceMonitor& getResourceMonitor();
    
    /**
     * @brief Get the scheduler
     * @return Reference to Scheduler
     */
    Scheduler& getScheduler();

private:
    /**
     * @brief Handle an IPC request
     * @param type Message type
     * @param data Request data (JSON)
     * @return Response data (JSON)
     */
    std::string handleRequest(MsgType type, const std::string& data);
    
    /**
     * @brief Handle SUBMIT request
     * @param data SubmitRequest JSON
     * @return Response JSON with task ID or error
     */
    std::string handleSubmit(const std::string& data);
    
    /**
     * @brief Handle QUERY_QUEUE request
     * @param include_completed If true, include completed/failed/cancelled tasks
     * @return QueueResponse JSON
     */
    std::string handleQueryQueue(bool include_completed = false);
    
    /**
     * @brief Handle DELETE_TASK request
     * @param data DeleteRequest JSON
     * @return Response JSON with deletion results
     */
    std::string handleDelete(const std::string& data);
    
    /**
     * @brief Handle DELETE_ALL request
     * @return DeleteAllResponse JSON
     */
    std::string handleDeleteAll();
    
    /**
     * @brief Handle GET_TASK_INFO request
     * @param data TaskInfoRequest JSON
     * @return TaskDetailResponse JSON
     */
    std::string handleGetTaskInfo(const std::string& data);
    
    /**
     * @brief Handle GET_TASK_LOG request
     * @param data TaskLogRequest JSON
     * @return TaskLogResponse JSON
     */
    std::string handleGetTaskLog(const std::string& data);
    
    /**
     * @brief Handle SHUTDOWN request
     * @return Response JSON
     */
    std::string handleShutdown();
    
    /**
     * @brief Set up signal handlers
     */
    void setupSignalHandlers();
    
    /**
     * @brief Get current timestamp string for logging
     * @return Formatted timestamp string
     */
    static std::string getTimestamp();
    
    /**
     * @brief Create log directory recursively
     * @param path Directory path to create
     * @return true if directory exists or was created successfully
     */
    static bool createLogDirectory(const std::string& path);
    
public:
    /**
     * @brief Write a message to the server log file
     * @param level Log level (INFO, WARN, ERROR, DEBUG)
     * @param message Log message
     */
    void log(const std::string& level, const std::string& message);
    
private:
    /// Server configuration
    Config config_;
    
    /// Task queue
    std::unique_ptr<TaskQueue> queue_;
    
    /// Resource monitor
    std::unique_ptr<ResourceMonitor> monitor_;
    
    /// Task executor
    std::unique_ptr<Executor> executor_;
    
    /// Task scheduler
    std::unique_ptr<Scheduler> scheduler_;
    
    /// IPC server
    std::unique_ptr<IPCServer> ipc_server_;
    
    /// Running flag
    std::atomic<bool> running_{false};
    
    /// Shutdown requested flag
    std::atomic<bool> shutdown_requested_{false};
};

/**
 * @brief Global server instance for signal handling
 */
extern Server* g_server_instance;

} // namespace myqueue

#endif // MYQUEUE_SERVER_H

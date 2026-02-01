/**
 * @file server.cpp
 * @brief Implementation of Server class
 * 
 * Main server daemon that integrates all myqueue components.
 */

#include "myqueue/server.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace myqueue {

// Global server instance for signal handling
Server* g_server_instance = nullptr;

// Signal handler
static void signalHandler(int signum) {
    if (g_server_instance) {
        g_server_instance->log("INFO", "Received signal " + std::to_string(signum) + ", shutting down...");
        g_server_instance->stop();
    }
}

Server::Server(const Config& config)
    : config_(config) {
    // Create data directory if needed
    struct stat st;
    if (stat(config_.data_dir.c_str(), &st) != 0) {
        std::string cmd = "mkdir -p " + config_.data_dir;
        int ret = system(cmd.c_str());
        if (ret != 0) {
            // Log warning but continue - will fail later if directory is really needed
        }
    }
    
    // Create log directory if logging is enabled
    if (config_.enable_logging && !config_.log_dir.empty()) {
        createLogDirectory(config_.log_dir);
    }
    
    // Initialize components
    queue_ = std::make_unique<TaskQueue>(config_.data_dir);
    monitor_ = std::make_unique<ResourceMonitor>(config_);
    executor_ = std::make_unique<Executor>(config_.log_dir, config_.enable_job_log);
    scheduler_ = std::make_unique<Scheduler>(
        *queue_, *monitor_, *executor_,
        config_.scheduling_interval_ms,
        config_.process_check_interval_ms);
    ipc_server_ = std::make_unique<IPCServer>(config_.socket_path);
    
    log("INFO", "Server initialized");
    log("DEBUG", "Config: socket_path=" + config_.socket_path + 
        ", data_dir=" + config_.data_dir + 
        ", log_dir=" + config_.log_dir +
        ", gpu_memory_threshold=" + std::to_string(config_.gpu_memory_threshold_mb) + "MB" +
        ", cpu_util_threshold=" + std::to_string(config_.cpu_util_threshold) + "%");
}

Server::~Server() {
    log("INFO", "Server shutting down");
    stop();
    if (g_server_instance == this) {
        g_server_instance = nullptr;
    }
}

bool Server::start() {
    if (running_.exchange(true)) {
        return true;  // Already running
    }
    
    log("INFO", "Starting server...");
    
    // Set global instance for signal handling
    g_server_instance = this;
    
    // Set up signal handlers
    setupSignalHandlers();
    
    // Load task queue from disk
    queue_->load();
    log("INFO", "Task queue loaded");
    
    // Restore resource allocations for running tasks
    auto running_tasks = queue_->getRunningTasks();
    log("DEBUG", "Found " + std::to_string(running_tasks.size()) + " running tasks from previous session");
    
    for (const auto& task : running_tasks) {
        // Check if process is still running
        auto status = executor_->checkStatus(task.pid);
        if (status.running) {
            log("INFO", "Task " + std::to_string(task.id) + " (PID " + std::to_string(task.pid) + ") is still running");
        } else {
            // Process died while server was down - mark as failed
            log("WARN", "Task " + std::to_string(task.id) + " (PID " + std::to_string(task.pid) + ") died while server was down");
            queue_->setTaskFailed(task.id);
        }
    }
    
    // Start scheduler
    scheduler_->start();
    log("INFO", "Scheduler started");
    
    // Start IPC server with request handler
    ipc_server_->start([this](MsgType type, const std::string& data) {
        return handleRequest(type, data);
    });
    log("INFO", "IPC server started on " + config_.socket_path);
    
    log("INFO", "Server started successfully");
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }
    
    log("INFO", "Stopping server...");
    shutdown_requested_ = true;
    
    // Stop IPC server first (stop accepting new requests)
    if (ipc_server_) {
        ipc_server_->stop();
        log("DEBUG", "IPC server stopped");
    }
    
    // Stop scheduler
    if (scheduler_) {
        scheduler_->stop();
        log("DEBUG", "Scheduler stopped");
    }
    
    // Save task queue to disk
    if (queue_) {
        queue_->save();
        log("DEBUG", "Task queue saved");
    }
    
    log("INFO", "Server stopped");
}

bool Server::isRunning() const {
    return running_.load();
}

void Server::run() {
    if (!start()) {
        return;
    }
    
    // Wait for shutdown signal
    while (running_.load() && !shutdown_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    stop();
}

bool Server::daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        return false;  // Fork failed
    }
    
    if (pid > 0) {
        // Parent process - exit
        _exit(0);
    }
    
    // Child process - become session leader
    if (setsid() < 0) {
        return false;
    }
    
    // Fork again to prevent acquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }
    
    // Change working directory to root
    if (chdir("/") != 0) {
        // Continue anyway, not critical
    }
    
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Redirect to /dev/null
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd > 2) {
            close(null_fd);
        }
    }
    
    return true;
}

const Config& Server::getConfig() const {
    return config_;
}

TaskQueue& Server::getTaskQueue() {
    return *queue_;
}

ResourceMonitor& Server::getResourceMonitor() {
    return *monitor_;
}

Scheduler& Server::getScheduler() {
    return *scheduler_;
}

std::string Server::handleRequest(MsgType type, const std::string& data) {
    log("DEBUG", "Received request: " + msgTypeToString(type));
    
    switch (type) {
        case MsgType::SUBMIT:
            return handleSubmit(data);
        case MsgType::QUERY_QUEUE:
            return handleQueryQueue(false);
        case MsgType::QUERY_QUEUE_ALL:
            return handleQueryQueue(true);
        case MsgType::DELETE_TASK:
            return handleDelete(data);
        case MsgType::DELETE_ALL:
            return handleDeleteAll();
        case MsgType::GET_TASK_INFO:
            return handleGetTaskInfo(data);
        case MsgType::GET_TASK_LOG:
            return handleGetTaskLog(data);
        case MsgType::SHUTDOWN:
            return handleShutdown();
        default:
            nlohmann::json response;
            response["success"] = false;
            response["error"] = "Unknown message type";
            return response.dump();
    }
}

std::string Server::handleSubmit(const std::string& data) {
    nlohmann::json response;
    
    try {
        auto req = SubmitRequest::fromJson(data);
        
        log("INFO", "Submit request: script=" + req.script_path + ", workdir=" + req.workdir +
            ", ncpu=" + std::to_string(req.ncpu) + ", ngpu=" + std::to_string(req.ngpu));
        
        // Validate script exists
        struct stat st;
        if (stat(req.script_path.c_str(), &st) != 0) {
            log("WARN", "Submit failed: script not found: " + req.script_path);
            response["success"] = false;
            response["error"] = "Script file not found: " + req.script_path;
            return response.dump();
        }
        
        // Validate workdir exists
        if (stat(req.workdir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            log("WARN", "Submit failed: workdir not found: " + req.workdir);
            response["success"] = false;
            response["error"] = "Working directory not found: " + req.workdir;
            return response.dump();
        }
        
        // Submit task
        uint64_t id = queue_->submit(req);
        queue_->save();
        
        log("INFO", "Task " + std::to_string(id) + " submitted successfully");
        
        response["success"] = true;
        response["task_id"] = id;
        
    } catch (const std::exception& e) {
        log("ERROR", "Submit failed: " + std::string(e.what()));
        response["success"] = false;
        response["error"] = std::string("Failed to parse request: ") + e.what();
    }
    
    return response.dump();
}

std::string Server::handleQueryQueue(bool include_completed) {
    QueueResponse qr;
    
    // Get current time for duration calculation
    auto now = std::chrono::system_clock::now();
    
    // Get running tasks
    auto running = queue_->getRunningTasks();
    for (const auto& task : running) {
        TaskInfo info;
        info.id = task.id;
        info.status = "running";
        info.script = task.script_path;
        info.workdir = task.workdir;
        info.cpus = task.allocated_cpus;
        info.gpus = task.allocated_gpus;
        info.exit_code = 0;
        
        // Calculate running duration
        if (task.start_time.has_value()) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                now - *task.start_time);
            info.duration_seconds = duration.count();
        } else {
            info.duration_seconds = 0;
        }
        
        qr.running.push_back(info);
    }
    
    // Get pending tasks
    auto pending = queue_->getPendingTasks();
    for (const auto& task : pending) {
        TaskInfo info;
        info.id = task.id;
        info.status = "pending";
        info.script = task.script_path;
        info.workdir = task.workdir;
        info.cpus = task.specific_cpus;
        info.gpus = task.specific_gpus;
        info.exit_code = 0;
        info.duration_seconds = 0;
        qr.pending.push_back(info);
    }
    
    // Get completed tasks (only if requested)
    if (include_completed) {
        auto all_tasks = queue_->getAllTasks();
        for (const auto& task : all_tasks) {
            if (task.status == TaskStatus::COMPLETED || 
                task.status == TaskStatus::FAILED ||
                task.status == TaskStatus::CANCELLED) {
                TaskInfo info;
                info.id = task.id;
                info.status = taskStatusToString(task.status);
                info.script = task.script_path;
                info.workdir = task.workdir;
                info.cpus = task.allocated_cpus;
                info.gpus = task.allocated_gpus;
                info.exit_code = task.exit_code;
                
                // Calculate duration
                if (task.start_time.has_value() && task.end_time.has_value()) {
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                        *task.end_time - *task.start_time);
                    info.duration_seconds = duration.count();
                }
                
                qr.completed.push_back(info);
            }
        }
    }
    
    return qr.toJson();
}

std::string Server::handleDelete(const std::string& data) {
    nlohmann::json response;
    
    try {
        auto req = DeleteRequest::fromJson(data);
        
        log("INFO", "Delete request for " + std::to_string(req.task_ids.size()) + " task(s)");
        
        std::vector<nlohmann::json> results;
        
        for (uint64_t id : req.task_ids) {
            nlohmann::json result;
            result["id"] = id;
            
            auto task = queue_->getTask(id);
            if (!task.has_value()) {
                log("WARN", "Delete task " + std::to_string(id) + ": not found");
                result["success"] = false;
                result["error"] = "Task not found";
            } else if (task->status == TaskStatus::RUNNING) {
                // Terminate running task
                log("INFO", "Terminating running task " + std::to_string(id));
                bool terminated = scheduler_->terminateTask(id, false);
                if (!terminated) {
                    // Force kill
                    log("WARN", "Force killing task " + std::to_string(id));
                    terminated = scheduler_->terminateTask(id, true);
                }
                result["success"] = terminated;
                if (!terminated) {
                    result["error"] = "Failed to terminate task";
                } else {
                    log("INFO", "Task " + std::to_string(id) + " terminated");
                }
            } else if (task->status == TaskStatus::PENDING) {
                // Delete pending task
                bool deleted = queue_->deleteTask(id);
                log("INFO", "Deleted pending task " + std::to_string(id));
                result["success"] = deleted;
            } else {
                // Task is in terminal state
                bool deleted = queue_->deleteTask(id);
                log("DEBUG", "Deleted completed task " + std::to_string(id));
                result["success"] = deleted;
            }
            
            results.push_back(result);
        }
        
        queue_->save();
        
        response["success"] = true;
        response["results"] = results;
        
    } catch (const std::exception& e) {
        log("ERROR", "Delete failed: " + std::string(e.what()));
        response["success"] = false;
        response["error"] = std::string("Failed to parse request: ") + e.what();
    }
    
    return response.dump();
}

std::string Server::handleShutdown() {
    log("INFO", "Shutdown request received");
    
    nlohmann::json response;
    response["success"] = true;
    response["message"] = "Server shutting down";
    
    // Request shutdown (will be processed after response is sent)
    shutdown_requested_ = true;
    
    return response.dump();
}

std::string Server::handleDeleteAll() {
    log("INFO", "Delete all tasks request received");
    
    DeleteAllResponse resp;
    
    // Get all tasks
    auto all_tasks = queue_->getAllTasks();
    
    for (const auto& task : all_tasks) {
        if (task.status == TaskStatus::RUNNING) {
            // Terminate running task
            log("INFO", "Terminating running task " + std::to_string(task.id));
            bool terminated = scheduler_->terminateTask(task.id, false);
            if (!terminated) {
                terminated = scheduler_->terminateTask(task.id, true);
            }
            if (terminated) {
                resp.running_terminated++;
                resp.deleted_count++;
            }
        } else if (task.status == TaskStatus::PENDING) {
            if (queue_->deleteTask(task.id)) {
                resp.pending_deleted++;
                resp.deleted_count++;
            }
        } else {
            // Completed/Failed/Cancelled
            if (queue_->deleteTask(task.id)) {
                resp.completed_deleted++;
                resp.deleted_count++;
            }
        }
    }
    
    queue_->save();
    
    log("INFO", "Deleted " + std::to_string(resp.deleted_count) + " tasks");
    
    return resp.toJson();
}

std::string Server::handleGetTaskInfo(const std::string& data) {
    TaskDetailResponse resp;
    
    try {
        auto req = TaskInfoRequest::fromJson(data);
        auto task = queue_->getTask(req.task_id);
        
        if (!task.has_value()) {
            resp.found = false;
            resp.id = req.task_id;
            return resp.toJson();
        }
        
        resp.found = true;
        resp.id = task->id;
        resp.status = taskStatusToString(task->status);
        resp.script = task->script_path;
        resp.workdir = task->workdir;
        resp.ncpu = task->ncpu;
        resp.ngpu = task->ngpu;
        resp.specific_cpus = task->specific_cpus;
        resp.specific_gpus = task->specific_gpus;
        resp.allocated_cpus = task->allocated_cpus;
        resp.allocated_gpus = task->allocated_gpus;
        resp.log_file = task->log_file;
        resp.exit_code = task->exit_code;
        resp.pid = task->pid;
        
        // Format timestamps
        auto formatTime = [](const std::chrono::system_clock::time_point& tp) -> std::string {
            auto time_t_val = std::chrono::system_clock::to_time_t(tp);
            std::tm tm_val;
            localtime_r(&time_t_val, &tm_val);
            std::ostringstream oss;
            oss << std::put_time(&tm_val, "%Y-%m-%d %H:%M:%S");
            return oss.str();
        };
        
        resp.submit_time = formatTime(task->submit_time);
        
        if (task->start_time.has_value()) {
            resp.start_time = formatTime(*task->start_time);
        }
        
        if (task->end_time.has_value()) {
            resp.end_time = formatTime(*task->end_time);
        }
        
        // Calculate duration
        if (task->start_time.has_value()) {
            auto end = task->end_time.has_value() ? *task->end_time : std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - *task->start_time);
            resp.duration_seconds = duration.count();
        }
        
    } catch (const std::exception& e) {
        log("ERROR", "GetTaskInfo failed: " + std::string(e.what()));
        resp.found = false;
    }
    
    return resp.toJson();
}

std::string Server::handleGetTaskLog(const std::string& data) {
    TaskLogResponse resp;
    
    try {
        auto req = TaskLogRequest::fromJson(data);
        auto task = queue_->getTask(req.task_id);
        
        if (!task.has_value()) {
            resp.found = false;
            resp.task_id = req.task_id;
            resp.error = "Task not found";
            return resp.toJson();
        }
        
        resp.task_id = task->id;
        
        // Determine log file path
        std::string log_path;
        if (!task->log_file.empty()) {
            log_path = task->workdir + "/" + task->log_file;
        } else if (config_.enable_job_log) {
            log_path = task->workdir + "/job.log";
        } else {
            // Check server log directory
            if (!config_.log_dir.empty()) {
                log_path = config_.log_dir + "/task_" + std::to_string(task->id) + ".out";
            }
        }
        
        if (log_path.empty()) {
            resp.found = false;
            resp.error = "No log file configured for this task";
            return resp.toJson();
        }
        
        resp.log_path = log_path;
        
        // Read log file
        std::ifstream file(log_path);
        if (!file.is_open()) {
            resp.found = false;
            resp.error = "Log file not found: " + log_path;
            return resp.toJson();
        }
        
        resp.found = true;
        
        if (req.tail_lines > 0) {
            // Read last N lines
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
                if (static_cast<int>(lines.size()) > req.tail_lines) {
                    lines.erase(lines.begin());
                }
            }
            for (const auto& l : lines) {
                resp.content += l + "\n";
            }
        } else {
            // Read entire file
            std::ostringstream ss;
            ss << file.rdbuf();
            resp.content = ss.str();
        }
        
    } catch (const std::exception& e) {
        log("ERROR", "GetTaskLog failed: " + std::string(e.what()));
        resp.found = false;
        resp.error = e.what();
    }
    
    return resp.toJson();
}

void Server::setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    
    // Ignore SIGPIPE (broken pipe)
    signal(SIGPIPE, SIG_IGN);
}

std::string Server::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

bool Server::createLogDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    // Create directory recursively
    size_t pos = 0;
    std::string current_path;
    
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        current_path = path.substr(0, pos);
        if (!current_path.empty()) {
            struct stat st;
            if (stat(current_path.c_str(), &st) != 0) {
                if (mkdir(current_path.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
    }
    
    // Create the final directory
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    
    return true;
}

void Server::log(const std::string& level, const std::string& message) {
    if (!config_.enable_logging || config_.log_dir.empty()) {
        return;
    }
    
    std::string log_path = config_.log_dir + "/server.log";
    std::ofstream log_file(log_path, std::ios::app);
    
    if (log_file.is_open()) {
        log_file << "[" << getTimestamp() << "] [" << level << "] " << message << "\n";
        log_file.flush();
    }
}

} // namespace myqueue

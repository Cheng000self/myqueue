/**
 * @file executor.cpp
 * @brief Implementation of Executor class
 * 
 * Handles process creation and management for task execution.
 */

#include "myqueue/executor.h"
#include "myqueue/errors.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace myqueue {

Executor::Executor(const std::string& log_dir, bool enable_job_log)
    : log_dir_(log_dir), enable_job_log_(enable_job_log) {
    // Create log directory if specified
    if (!log_dir_.empty()) {
        createLogDirectory();
    }
}

bool Executor::createLogDirectory() {
    if (log_dir_.empty()) {
        return false;
    }
    
    // Create directory recursively
    size_t pos = 0;
    std::string current_path;
    
    while ((pos = log_dir_.find('/', pos + 1)) != std::string::npos) {
        current_path = log_dir_.substr(0, pos);
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
    if (stat(log_dir_.c_str(), &st) != 0) {
        if (mkdir(log_dir_.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    
    return true;
}

std::string Executor::getTimestamp() {
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

void Executor::writeServerLog(const std::string& level, const std::string& message) {
    if (log_dir_.empty()) {
        return;
    }
    
    std::string log_path = log_dir_ + "/server.log";
    std::ofstream log_file(log_path, std::ios::app);
    
    if (log_file.is_open()) {
        log_file << "[" << getTimestamp() << "] [" << level << "] " << message << "\n";
        log_file.flush();
    }
}

std::string Executor::buildCpuString(const std::vector<int>& cpus) {
    std::string result;
    for (size_t i = 0; i < cpus.size(); ++i) {
        if (i > 0) result += ",";
        result += std::to_string(cpus[i]);
    }
    return result;
}

std::string Executor::buildGpuString(const std::vector<int>& gpus) {
    std::string result;
    for (size_t i = 0; i < gpus.size(); ++i) {
        if (i > 0) result += ",";
        result += std::to_string(gpus[i]);
    }
    return result;
}

void Executor::setupEnvironment(const std::vector<int>& cpus,
                                 const std::vector<int>& gpus) {
    // Set CUDA_VISIBLE_DEVICES for GPU visibility
    std::string gpu_str = buildGpuString(gpus);
    setenv("CUDA_VISIBLE_DEVICES", gpu_str.c_str(), 1);
    
    // Set MYQUEUE_GPUS (same as CUDA_VISIBLE_DEVICES)
    setenv("MYQUEUE_GPUS", gpu_str.c_str(), 1);
    
    // Set MYQUEUE_CPUS for CPU information
    std::string cpu_str = buildCpuString(cpus);
    setenv("MYQUEUE_CPUS", cpu_str.c_str(), 1);
}

void Executor::setupLogging(uint64_t task_id) {
    if (log_dir_.empty()) {
        return;
    }
    
    // Create log directory if needed (should already exist, but ensure)
    createLogDirectory();
    
    // Redirect stdout to log file
    std::string stdout_path = log_dir_ + "/task_" + std::to_string(task_id) + ".out";
    int stdout_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stdout_fd >= 0) {
        dup2(stdout_fd, STDOUT_FILENO);
        close(stdout_fd);
    }
    
    // Redirect stderr to log file
    std::string stderr_path = log_dir_ + "/task_" + std::to_string(task_id) + ".err";
    int stderr_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd >= 0) {
        dup2(stderr_fd, STDERR_FILENO);
        close(stderr_fd);
    }
}

pid_t Executor::execute(const Task& task,
                        const std::vector<int>& cpus,
                        const std::vector<int>& gpus) {
    // Log task execution start
    std::ostringstream log_msg;
    log_msg << "Starting task " << task.id 
            << " | script: " << task.script_path
            << " | workdir: " << task.workdir
            << " | cpus: " << buildCpuString(cpus)
            << " | gpus: " << buildGpuString(gpus);
    writeServerLog("INFO", log_msg.str());
    
    // Determine if we should write job log
    std::string job_log_file;
    if (!task.log_file.empty()) {
        job_log_file = task.log_file;
    } else if (enable_job_log_) {
        job_log_file = "job.log";
    }
    
    // Record start time
    auto start_time = std::chrono::system_clock::now();
    
    pid_t pid = fork();
    
    if (pid < 0) {
        // Fork failed
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        
        // Create a new process group so we can kill all child processes later
        // setpgid(0, 0) makes this process the leader of a new process group
        setpgid(0, 0);
        
        // Set up environment variables
        setupEnvironment(cpus, gpus);
        
        // Change to working directory first
        if (chdir(task.workdir.c_str()) != 0) {
            _exit(127);  // Exit with error code indicating chdir failure
        }
        
        // Set up job log if enabled
        if (!job_log_file.empty()) {
            // Open job log file in workdir
            int log_fd = open(job_log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (log_fd >= 0) {
                // Write job header
                std::ostringstream header;
                header << "================================================================================\n"
                       << "MyQueue Job Log\n"
                       << "================================================================================\n"
                       << "Task ID:     " << task.id << "\n"
                       << "Script:      " << task.script_path << "\n"
                       << "Workdir:     " << task.workdir << "\n"
                       << "CPUs:        " << buildCpuString(cpus) << " (" << cpus.size() << " cores)\n"
                       << "GPUs:        " << buildGpuString(gpus) << " (" << gpus.size() << " devices)\n"
                       << "Start Time:  " << getTimestamp() << "\n"
                       << "================================================================================\n\n";
                std::string header_str = header.str();
                (void)write(log_fd, header_str.c_str(), header_str.size());
                
                // Redirect stdout and stderr to log file
                dup2(log_fd, STDOUT_FILENO);
                dup2(log_fd, STDERR_FILENO);
                close(log_fd);
            }
        } else {
            // Set up server logging if configured (old behavior)
            setupLogging(task.id);
        }
        
        // Execute the script via bash
        execl("/bin/bash", "bash", task.script_path.c_str(), nullptr);
        
        // If execl returns, it failed
        _exit(126);  // Exit with error code indicating exec failure
    }
    
    // Parent process
    // Also call setpgid in parent to avoid race condition
    // This ensures the child is in its own process group before we try to use it
    setpgid(pid, pid);
    
    // Return child PID
    writeServerLog("INFO", "Task " + std::to_string(task.id) + " started with PID " + std::to_string(pid));
    return pid;
}

ProcessStatus Executor::checkStatus(pid_t pid) {
    ProcessStatus status;
    status.running = true;
    status.exit_code = 0;
    status.signaled = false;
    status.signal_number = 0;
    
    int wstatus;
    pid_t result = waitpid(pid, &wstatus, WNOHANG);
    
    if (result == 0) {
        // Process is still running
        return status;
    }
    
    if (result < 0) {
        // Error or process doesn't exist
        status.running = false;
        status.exit_code = -1;
        return status;
    }
    
    // Process has terminated
    status.running = false;
    
    if (WIFEXITED(wstatus)) {
        status.exit_code = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        status.signaled = true;
        status.signal_number = WTERMSIG(wstatus);
        status.exit_code = 128 + status.signal_number;
    }
    
    return status;
}

bool Executor::terminate(pid_t pid, bool force) {
    int sig = force ? SIGKILL : SIGTERM;
    
    // Kill the entire process group by using negative PID
    // This ensures all child processes (like mpirun, vasp) are also terminated
    // First try to kill the process group
    int result = kill(-pid, sig);
    
    if (result != 0) {
        // If killing process group failed, try killing just the process
        // This can happen if the process didn't create a new process group
        result = kill(pid, sig);
    }
    
    return result == 0;
}

std::optional<int> Executor::waitFor(pid_t pid, int timeout_ms) {
    if (timeout_ms == 0) {
        // Non-blocking check
        auto status = checkStatus(pid);
        if (!status.running) {
            return status.exit_code;
        }
        return std::nullopt;
    }
    
    if (timeout_ms < 0) {
        // Blocking wait
        int wstatus;
        pid_t result = waitpid(pid, &wstatus, 0);
        
        if (result <= 0) {
            return std::nullopt;
        }
        
        if (WIFEXITED(wstatus)) {
            return WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
            return 128 + WTERMSIG(wstatus);
        }
        return -1;
    }
    
    // Timed wait - poll with sleep
    int elapsed = 0;
    const int poll_interval = 100;  // 100ms
    
    while (elapsed < timeout_ms) {
        auto status = checkStatus(pid);
        if (!status.running) {
            return status.exit_code;
        }
        
        usleep(poll_interval * 1000);
        elapsed += poll_interval;
    }
    
    return std::nullopt;
}

} // namespace myqueue

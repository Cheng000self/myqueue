/**
 * @file main.cpp
 * @brief myqueue - GPU Task Queue System CLI Entry Point
 * 
 * 用户级别的GPU计算任务队列系统命令行入口
 * 
 * 命令:
 *   myqueue server [--log <path>] [--gpumemory <MB>] [--cpuusage <percent>] [--init]
 *   myqueue sb <script> [--ncpu N] [--ngpu M] [--cpus "x,y"] [--gpus "x,y"] [-w <dir>] [-ws <file>]
 *   myqueue sq [all]
 *   myqueue del <id> | <start>-<end> | all
 *   myqueue init
 */

#include "myqueue/config.h"
#include "myqueue/server.h"
#include "myqueue/ipc_client.h"
#include "myqueue/protocol.h"
#include "myqueue/task_queue.h"
#include "myqueue/resource_monitor.h"
#include "json.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

namespace myqueue {

// Version information
const char* VERSION = "1.0.0";
const char* AUTHOR = "rcz";

// ANSI color codes
namespace Color {
    // Check if output is a terminal
    inline bool isTerminal() {
        return isatty(fileno(stdout)) != 0;
    }
    
    inline std::string green() { return isTerminal() ? "\033[32m" : ""; }
    inline std::string yellow() { return isTerminal() ? "\033[33m" : ""; }
    inline std::string red() { return isTerminal() ? "\033[31m" : ""; }
    inline std::string cyan() { return isTerminal() ? "\033[36m" : ""; }
    inline std::string gray() { return isTerminal() ? "\033[90m" : ""; }
    inline std::string reset() { return isTerminal() ? "\033[0m" : ""; }
}

void printVersion() {
    std::cout << "myqueue version " << VERSION << "\n"
              << "Author: " << AUTHOR << "\n"
              << "A user-level GPU task queue system\n";
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <command> [options]\n"
              << "\n"
              << "Commands:\n"
              << "  server    Start the background daemon service\n"
              << "  stop      Stop the running server\n"
              << "  init      Initialize/reset myqueue data (clear all tasks)\n"
              << "  sb        Submit a task to the queue\n"
              << "  sq        Query the task queue (sq all: show all including completed)\n"
              << "            Options: -s, --summary (show only summary line)\n"
              << "  del       Delete task(s) from the queue (del all: delete all tasks)\n"
              << "  info      Show detailed task information\n"
              << "  log       Show task log output\n"
              << "  res       Show current resource status (CPU/GPU)\n"
              << "\n"
              << "Server options:\n"
              << "  --log <path>         Write logs to the specified directory\n"
              << "  --joblog             Enable job log output to workdir (default: off)\n"
              << "  --gpumemory <MB>     GPU busy threshold (default: 2000MB)\n"
              << "  --cpuusage <percent> CPU idle threshold (default: 40%)\n"
              << "  --foreground         Run in foreground (don't daemonize)\n"
              << "  --init               Initialize queue data before starting\n"
              << "\n"
              << "Submit options:\n"
              << "  --ncpu N             Number of CPU cores (default: 1)\n"
              << "  --ngpu N             Number of GPU devices (default: 1)\n"
              << "  --cpus \"x,y,z\"       Specific CPU cores to use\n"
              << "  --gpus \"x,y,z\"       Specific GPU devices to use\n"
              << "  -w, --workdir <path> Working directory for the task\n"
              << "  -ws, --workdirs <file> File containing list of working directories\n"
              << "  --logfile <name>     Job log file name (default: job.log when enabled)\n"
              << "\n"
              << "Delete options:\n"
              << "  <id>                 Delete task with specific ID\n"
              << "  <start>-<end>        Delete tasks in ID range\n"
              << "  all                  Delete all tasks (running, pending, completed)\n"
              << "\n"
              << "Log options:\n"
              << "  -n, --tail <lines>   Show last N lines only\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << " server --log ~/.myqueue/logs\n"
              << "  " << program << " server --init          # Start with clean queue\n"
              << "  " << program << " init                   # Reset queue data\n"
              << "  " << program << " sb job.sh --ncpu 4 --ngpu 2\n"
              << "  " << program << " sb job.sh -w /home/user/calc\n"
              << "  " << program << " sb job.sh -ws workdirs.txt\n"
              << "  " << program << " sq                     # Show running/pending\n"
              << "  " << program << " sq -s                  # Show summary only\n"
              << "  " << program << " sq all                 # Show all tasks\n"
              << "  " << program << " info 5                 # Show task 5 details\n"
              << "  " << program << " log 5                  # Show task 5 log\n"
              << "  " << program << " log 5 -n 50           # Show last 50 lines\n"
              << "  " << program << " del 5\n"
              << "  " << program << " del 1-10\n"
              << "  " << program << " del all                # Delete all tasks\n"
              << "  " << program << " stop\n"
              << "  " << program << " res\n"
              << "  " << program << " --version\n";
}

// Parse comma-separated integers
std::vector<int> parseIntList(const std::string& str) {
    std::vector<int> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stoi(item));
        } catch (...) {
            // Skip invalid entries
        }
    }
    return result;
}

// Get absolute path
std::string getAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    if (path[0] == '/') {
        return path;
    }
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        return std::string(cwd) + "/" + path;
    }
    return path;
}

// Server command
int handleServer(int argc, char* argv[]) {
    Config config = Config::fromArgs(argc, argv);
    bool foreground = false;
    bool init_queue = false;
    
    // Parse additional arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--foreground" || arg == "-f") {
            foreground = true;
        } else if (arg == "--init") {
            init_queue = true;
        }
    }
    
    // Check if server is already running
    IPCClient client(config.socket_path);
    if (client.connect()) {
        std::cerr << "Error: Server is already running\n";
        return 1;
    }
    
    // Initialize queue if requested
    if (init_queue) {
        std::cout << "Initializing queue data...\n";
        std::string tasks_file = config.data_dir + "/tasks.json";
        std::remove(tasks_file.c_str());
    }
    
    std::cout << "Starting myqueue server...\n";
    std::cout << "  Socket: " << config.socket_path << "\n";
    std::cout << "  Data dir: " << config.data_dir << "\n";
    if (config.enable_logging) {
        std::cout << "  Log dir: " << config.log_dir << "\n";
    }
    std::cout << "  Job log: " << (config.enable_job_log ? "enabled" : "disabled") << "\n";
    std::cout << "  GPU memory threshold: " << config.gpu_memory_threshold_mb << " MB\n";
    std::cout << "  CPU usage threshold: " << config.cpu_util_threshold << "%\n";
    
    Server server(config);
    
    if (!foreground) {
        std::cout << "Daemonizing...\n";
        if (!server.daemonize()) {
            std::cerr << "Error: Failed to daemonize\n";
            return 1;
        }
    }
    
    server.run();
    return 0;
}

// Init command - initialize/reset myqueue data
int handleInit(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    Config config = Config::fromArgs(0, nullptr);
    
    // Check if server is running
    IPCClient client(config.socket_path);
    if (client.connect()) {
        std::cerr << "Error: Server is running. Please stop the server first with 'myqueue stop'\n";
        return 1;
    }
    
    std::cout << "Initializing myqueue data...\n";
    std::cout << "  Data dir: " << config.data_dir << "\n";
    
    // Remove tasks.json file
    std::string tasks_file = config.data_dir + "/tasks.json";
    if (std::remove(tasks_file.c_str()) == 0) {
        std::cout << "  Removed: " << tasks_file << "\n";
    }
    
    // Remove socket file if exists
    if (std::remove(config.socket_path.c_str()) == 0) {
        std::cout << "  Removed: " << config.socket_path << "\n";
    }
    
    std::cout << "Initialization complete. Queue is now empty.\n";
    return 0;
}

// Submit command
int handleSubmit(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing script path\n";
        std::cerr << "Usage: myqueue sb <script> [options]\n";
        return 1;
    }
    
    std::string script = getAbsolutePath(argv[2]);
    int ncpu = 1;
    int ngpu = 1;
    std::vector<int> specific_cpus;
    std::vector<int> specific_gpus;
    std::string workdir;
    std::string workdirs_file;
    std::string log_file;
    
    // Parse arguments
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        
        if ((arg == "--ncpu" || arg == "-n") && i + 1 < argc) {
            ncpu = std::stoi(argv[++i]);
        } else if ((arg == "--ngpu" || arg == "-g") && i + 1 < argc) {
            ngpu = std::stoi(argv[++i]);
        } else if (arg == "--cpus" && i + 1 < argc) {
            specific_cpus = parseIntList(argv[++i]);
        } else if (arg == "--gpus" && i + 1 < argc) {
            specific_gpus = parseIntList(argv[++i]);
        } else if ((arg == "-w" || arg == "--workdir") && i + 1 < argc) {
            workdir = getAbsolutePath(argv[++i]);
        } else if ((arg == "-ws" || arg == "--workdirs") && i + 1 < argc) {
            workdirs_file = getAbsolutePath(argv[++i]);
        } else if (arg == "--logfile" && i + 1 < argc) {
            log_file = argv[++i];
        }
    }
    
    // Default workdir to current directory
    if (workdir.empty() && workdirs_file.empty()) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            workdir = cwd;
        }
    }
    
    // Connect to server
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Cannot connect to server. Is the server running?\n";
        std::cerr << "Start the server with: myqueue server\n";
        return 1;
    }
    
    // Handle batch submission
    if (!workdirs_file.empty()) {
        auto [valid_dirs, invalid_dirs] = TaskQueue::parseWorkdirsFile(workdirs_file);
        
        if (valid_dirs.empty()) {
            std::cerr << "Error: No valid directories found in " << workdirs_file << "\n";
            return 1;
        }
        
        // Report invalid directories
        for (const auto& dir : invalid_dirs) {
            std::cerr << "Warning: Skipping non-existent directory: " << dir << "\n";
        }
        
        // Submit each task
        std::vector<uint64_t> submitted_ids;
        for (const auto& dir : valid_dirs) {
            SubmitRequest req;
            req.script_path = script;
            req.workdir = dir;
            req.ncpu = ncpu;
            req.ngpu = ngpu;
            req.specific_cpus = specific_cpus;
            req.specific_gpus = specific_gpus;
            req.log_file = log_file;
            
            auto result = client.submit(req);
            if (result.has_value()) {
                submitted_ids.push_back(*result);
            } else {
                std::cerr << "Warning: Failed to submit task for " << dir << "\n";
            }
        }
        
        std::cout << "Submitted " << submitted_ids.size() << " tasks\n";
        if (!submitted_ids.empty()) {
            std::cout << "Task IDs: ";
            for (size_t i = 0; i < submitted_ids.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << submitted_ids[i];
            }
            std::cout << "\n";
        }
    } else {
        // Single task submission
        SubmitRequest req;
        req.script_path = script;
        req.workdir = workdir;
        req.ncpu = ncpu;
        req.ngpu = ngpu;
        req.specific_cpus = specific_cpus;
        req.specific_gpus = specific_gpus;
        req.log_file = log_file;
        
        auto result = client.submit(req);
        if (result.has_value()) {
            std::cout << "Task " << *result << " submitted\n";
        } else {
            std::cerr << "Error: Failed to submit task\n";
            return 1;
        }
    }
    
    return 0;
}

// Queue query command
int handleQueue(int argc, char* argv[]) {
    bool show_all = false;
    bool summary_only = false;
    
    // Check for arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "all") {
            show_all = true;
        } else if (arg == "-s" || arg == "--summary") {
            summary_only = true;
        }
    }
    
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Cannot connect to server. Is the server running?\n";
        return 1;
    }
    
    auto response = client.queryQueue(show_all);
    if (!response.has_value()) {
        std::cerr << "Error: Failed to query queue\n";
        return 1;
    }
    
    const auto& qr = *response;
    
    size_t running_count = qr.running.size();
    size_t pending_count = qr.pending.size();
    size_t completed_count = qr.completed.size();
    
    // If summary only mode, just print the summary
    if (summary_only) {
        std::cout << "Total: "
                  << Color::green() << running_count << " running" << Color::reset() << ", "
                  << Color::yellow() << pending_count << " pending" << Color::reset();
        if (show_all) {
            std::cout << ", " << Color::cyan() << completed_count << " completed" << Color::reset();
        }
        std::cout << "\n";
        return 0;
    }
    
    // Helper function to calculate duration
    auto calculateDuration = [](const TaskInfo& task) -> std::string {
        if (task.status == "running" && task.duration_seconds > 0) {
            int64_t seconds = task.duration_seconds;
            int h = seconds / 3600;
            int m = (seconds % 3600) / 60;
            int s = seconds % 60;
            std::ostringstream oss;
            oss << std::setfill('0') << std::setw(2) << h << ":"
                << std::setfill('0') << std::setw(2) << m << ":"
                << std::setfill('0') << std::setw(2) << s;
            return oss.str();
        }
        return "-";
    };
    
    // Print header
    if (show_all) {
        std::cout << std::left
                  << std::setw(8) << "ID"
                  << std::setw(12) << "STATUS"
                  << std::setw(10) << "EXIT"
                  << std::setw(12) << "DURATION"
                  << std::setw(20) << "CPUS"
                  << std::setw(15) << "GPUS"
                  << "WORKDIR\n";
        std::cout << std::string(100, '-') << "\n";
    } else {
        std::cout << std::left
                  << std::setw(8) << "ID"
                  << std::setw(12) << "STATUS"
                  << std::setw(12) << "DURATION"
                  << std::setw(20) << "CPUS"
                  << std::setw(15) << "GPUS"
                  << "WORKDIR\n";
        std::cout << std::string(80, '-') << "\n";
    }
    
    // Print running tasks first
    for (const auto& task : qr.running) {
        std::string cpus_str;
        for (size_t i = 0; i < task.cpus.size(); ++i) {
            if (i > 0) cpus_str += ",";
            cpus_str += std::to_string(task.cpus[i]);
        }
        
        std::string gpus_str;
        for (size_t i = 0; i < task.gpus.size(); ++i) {
            if (i > 0) gpus_str += ",";
            gpus_str += std::to_string(task.gpus[i]);
        }
        
        std::string duration_str = calculateDuration(task);
        
        std::cout << std::left
                  << std::setw(8) << task.id
                  << Color::green() << std::setw(12) << "RUNNING" << Color::reset();
        
        if (show_all) {
            std::cout << std::setw(10) << "-";
        }
        
        std::cout << std::setw(12) << duration_str
                  << std::setw(20) << cpus_str
                  << std::setw(15) << gpus_str
                  << task.workdir << "\n";
    }
    
    // Print pending tasks
    for (const auto& task : qr.pending) {
        std::string cpus_str = "-";
        if (!task.cpus.empty()) {
            cpus_str = "";
            for (size_t i = 0; i < task.cpus.size(); ++i) {
                if (i > 0) cpus_str += ",";
                cpus_str += std::to_string(task.cpus[i]);
            }
        }
        
        std::string gpus_str = "-";
        if (!task.gpus.empty()) {
            gpus_str = "";
            for (size_t i = 0; i < task.gpus.size(); ++i) {
                if (i > 0) gpus_str += ",";
                gpus_str += std::to_string(task.gpus[i]);
            }
        }
        
        std::cout << std::left
                  << std::setw(8) << task.id
                  << Color::yellow() << std::setw(12) << "PENDING" << Color::reset();
        
        if (show_all) {
            std::cout << std::setw(10) << "-";
        }
        
        std::cout << std::setw(12) << "-"
                  << std::setw(20) << cpus_str
                  << std::setw(15) << gpus_str
                  << task.workdir << "\n";
    }
    
    // Print completed tasks (only in "all" mode)
    if (show_all) {
        for (const auto& task : qr.completed) {
            std::string cpus_str = "-";
            if (!task.cpus.empty()) {
                cpus_str = "";
                for (size_t i = 0; i < task.cpus.size(); ++i) {
                    if (i > 0) cpus_str += ",";
                    cpus_str += std::to_string(task.cpus[i]);
                }
            }
            
            std::string gpus_str = "-";
            if (!task.gpus.empty()) {
                gpus_str = "";
                for (size_t i = 0; i < task.gpus.size(); ++i) {
                    if (i > 0) gpus_str += ",";
                    gpus_str += std::to_string(task.gpus[i]);
                }
            }
            
            // Determine status color
            std::string status_color;
            std::string status_str;
            if (task.status == "completed") {
                status_color = task.exit_code == 0 ? Color::cyan() : Color::red();
                status_str = task.exit_code == 0 ? "COMPLETED" : "FAILED";
            } else if (task.status == "cancelled") {
                status_color = Color::gray();
                status_str = "CANCELLED";
            } else {
                status_color = Color::gray();
                status_str = task.status;
            }
            
            // Format exit code
            std::string exit_str = std::to_string(task.exit_code);
            
            // Format duration
            std::string duration_str = "-";
            if (task.duration_seconds > 0) {
                int h = task.duration_seconds / 3600;
                int m = (task.duration_seconds % 3600) / 60;
                int s = task.duration_seconds % 60;
                std::ostringstream oss;
                oss << std::setfill('0') << std::setw(2) << h << ":"
                    << std::setfill('0') << std::setw(2) << m << ":"
                    << std::setfill('0') << std::setw(2) << s;
                duration_str = oss.str();
            }
            
            std::cout << std::left
                      << std::setw(8) << task.id
                      << status_color << std::setw(12) << status_str << Color::reset()
                      << std::setw(10) << exit_str
                      << std::setw(12) << duration_str
                      << std::setw(20) << cpus_str
                      << std::setw(15) << gpus_str
                      << task.workdir << "\n";
        }
    }
    
    if (running_count == 0 && pending_count == 0 && completed_count == 0) {
        std::cout << "Queue is empty\n";
    } else {
        std::cout << "\nTotal: "
                  << Color::green() << running_count << " running" << Color::reset() << ", "
                  << Color::yellow() << pending_count << " pending" << Color::reset();
        if (show_all) {
            std::cout << ", " << Color::cyan() << completed_count << " completed" << Color::reset();
        }
        std::cout << "\n";
    }
    
    return 0;
}

// Stop command
int handleStop(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Server is not running\n";
        return 1;
    }
    
    std::cout << "Stopping myqueue server...\n";
    
    if (client.shutdown()) {
        std::cout << "Server shutdown request sent successfully\n";
        return 0;
    } else {
        std::cerr << "Error: Failed to send shutdown request: " << client.lastError() << "\n";
        return 1;
    }
}

// Resource status command
int handleResource(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    Config config = Config::fromArgs(0, nullptr);
    ResourceMonitor monitor(config);
    
    // Get GPU status
    std::cout << "=== GPU Status ===\n";
    std::cout << std::left
              << std::setw(6) << "ID"
              << std::setw(12) << "STATUS"
              << std::setw(20) << "MEMORY"
              << "USAGE\n";
    std::cout << std::string(60, '-') << "\n";
    
    auto gpus = monitor.getGPUStatus();
    if (gpus.empty()) {
        std::cout << "No GPUs detected (nvidia-smi not available or no NVIDIA GPUs)\n";
    } else {
        int idle_gpus = 0;
        int busy_gpus = 0;
        
        for (const auto& gpu : gpus) {
            std::string status = gpu.is_busy ? "BUSY" : "IDLE";
            if (gpu.is_busy) {
                busy_gpus++;
            } else {
                idle_gpus++;
            }
            
            std::string memory_str = std::to_string(gpu.memory_used_mb) + "/" + 
                                     std::to_string(gpu.memory_total_mb) + " MB";
            
            double usage_percent = 0.0;
            if (gpu.memory_total_mb > 0) {
                usage_percent = 100.0 * gpu.memory_used_mb / gpu.memory_total_mb;
            }
            
            std::ostringstream usage_oss;
            usage_oss << std::fixed << std::setprecision(1) << usage_percent << "%";
            
            std::cout << std::left
                      << std::setw(6) << gpu.device_id
                      << std::setw(12) << status
                      << std::setw(20) << memory_str
                      << usage_oss.str() << "\n";
        }
        
        std::cout << "\nGPU Summary: " << idle_gpus << " idle, " << busy_gpus << " busy"
                  << " (threshold: " << config.gpu_memory_threshold_mb << " MB)\n";
    }
    
    // Get CPU status
    std::cout << "\n=== CPU Status ===\n";
    
    auto cpus = monitor.getCPUStatus();
    if (cpus.empty()) {
        std::cout << "No CPU information available\n";
    } else {
        // Group CPUs by affinity group
        std::vector<CPUInfo> group1_cpus, group2_cpus;
        int group1_idle = 0, group1_busy = 0;
        int group2_idle = 0, group2_busy = 0;
        
        for (const auto& cpu : cpus) {
            bool is_busy = cpu.utilization > config.cpu_util_threshold;
            if (cpu.affinity_group == 1) {
                group1_cpus.push_back(cpu);
                if (is_busy) group1_busy++; else group1_idle++;
            } else {
                group2_cpus.push_back(cpu);
                if (is_busy) group2_busy++; else group2_idle++;
            }
        }
        
        // Print Group 1 (CPU 0-31, GPU 0-3)
        std::cout << "Group 1 (CPU 0-31, GPU 0-3 affinity):\n";
        std::cout << "  Idle: ";
        bool first = true;
        for (const auto& cpu : group1_cpus) {
            if (cpu.utilization <= config.cpu_util_threshold) {
                if (!first) std::cout << ",";
                std::cout << cpu.core_id;
                first = false;
            }
        }
        if (first) std::cout << "(none)";
        std::cout << "\n";
        
        std::cout << "  Busy: ";
        first = true;
        for (const auto& cpu : group1_cpus) {
            if (cpu.utilization > config.cpu_util_threshold) {
                if (!first) std::cout << ",";
                std::cout << cpu.core_id;
                first = false;
            }
        }
        if (first) std::cout << "(none)";
        std::cout << "\n";
        std::cout << "  Summary: " << group1_idle << " idle, " << group1_busy << " busy\n";
        
        // Print Group 2 (CPU 32-63, GPU 4-7)
        std::cout << "\nGroup 2 (CPU 32-63, GPU 4-7 affinity):\n";
        std::cout << "  Idle: ";
        first = true;
        for (const auto& cpu : group2_cpus) {
            if (cpu.utilization <= config.cpu_util_threshold) {
                if (!first) std::cout << ",";
                std::cout << cpu.core_id;
                first = false;
            }
        }
        if (first) std::cout << "(none)";
        std::cout << "\n";
        
        std::cout << "  Busy: ";
        first = true;
        for (const auto& cpu : group2_cpus) {
            if (cpu.utilization > config.cpu_util_threshold) {
                if (!first) std::cout << ",";
                std::cout << cpu.core_id;
                first = false;
            }
        }
        if (first) std::cout << "(none)";
        std::cout << "\n";
        std::cout << "  Summary: " << group2_idle << " idle, " << group2_busy << " busy\n";
        
        std::cout << "\nCPU Total: " << (group1_idle + group2_idle) << " idle, " 
                  << (group1_busy + group2_busy) << " busy"
                  << " (threshold: " << config.cpu_util_threshold << "%)\n";
    }
    
    return 0;
}

// Delete command
int handleDelete(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing task ID\n";
        std::cerr << "Usage: myqueue del <id> | <start>-<end> | all\n";
        return 1;
    }
    
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Cannot connect to server. Is the server running?\n";
        return 1;
    }
    
    // Check for "all" argument
    std::string first_arg = argv[2];
    if (first_arg == "all") {
        // Delete all tasks
        std::cout << "Deleting all tasks...\n";
        
        auto result = client.deleteAll();
        if (result.has_value()) {
            std::cout << "Deleted " << result->deleted_count << " tasks ("
                      << result->running_terminated << " running terminated, "
                      << result->pending_deleted << " pending deleted, "
                      << result->completed_deleted << " completed deleted)\n";
            return 0;
        } else {
            std::cerr << "Error: Failed to delete all tasks\n";
            return 1;
        }
    }
    
    // Parse task IDs
    std::vector<uint64_t> task_ids;
    for (int i = 2; i < argc; ++i) {
        auto ids = TaskQueue::parseIdRange(argv[i]);
        task_ids.insert(task_ids.end(), ids.begin(), ids.end());
    }
    
    if (task_ids.empty()) {
        std::cerr << "Error: No valid task IDs specified\n";
        return 1;
    }
    
    auto results = client.deleteTasks(task_ids);
    
    int success_count = 0;
    int fail_count = 0;
    
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i]) {
            std::cout << "Task " << task_ids[i] << " deleted\n";
            success_count++;
        } else {
            std::cerr << "Failed to delete task " << task_ids[i] << "\n";
            fail_count++;
        }
    }
    
    if (task_ids.size() > 1) {
        std::cout << "\nDeleted " << success_count << " tasks";
        if (fail_count > 0) {
            std::cout << ", " << fail_count << " failed";
        }
        std::cout << "\n";
    }
    
    return fail_count > 0 ? 1 : 0;
}

// Info command - show detailed task information
int handleInfo(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing task ID\n";
        std::cerr << "Usage: myqueue info <id>\n";
        return 1;
    }
    
    uint64_t task_id;
    try {
        task_id = std::stoull(argv[2]);
    } catch (...) {
        std::cerr << "Error: Invalid task ID: " << argv[2] << "\n";
        return 1;
    }
    
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Cannot connect to server. Is the server running?\n";
        return 1;
    }
    
    auto response = client.getTaskInfo(task_id);
    if (!response.has_value()) {
        std::cerr << "Error: Failed to get task info: " << client.lastError() << "\n";
        return 1;
    }
    
    const auto& info = *response;
    
    if (!info.found) {
        std::cerr << "Error: Task " << task_id << " not found\n";
        return 1;
    }
    
    // Helper to format list of ints
    auto formatIntList = [](const std::vector<int>& list) -> std::string {
        if (list.empty()) return "-";
        std::string result;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) result += ",";
            result += std::to_string(list[i]);
        }
        return result;
    };
    
    // Helper to format duration
    auto formatDuration = [](int64_t seconds) -> std::string {
        if (seconds <= 0) return "-";
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        int s = seconds % 60;
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << h << ":"
            << std::setfill('0') << std::setw(2) << m << ":"
            << std::setfill('0') << std::setw(2) << s;
        return oss.str();
    };
    
    // Determine status color
    std::string status_color;
    if (info.status == "running") {
        status_color = Color::green();
    } else if (info.status == "pending") {
        status_color = Color::yellow();
    } else if (info.status == "completed") {
        status_color = info.exit_code == 0 ? Color::cyan() : Color::red();
    } else {
        status_color = Color::gray();
    }
    
    std::cout << "=== Task " << info.id << " ===\n";
    std::cout << std::left << std::setw(20) << "Status:" 
              << status_color << info.status << Color::reset() << "\n";
    std::cout << std::setw(20) << "Script:" << info.script << "\n";
    std::cout << std::setw(20) << "Workdir:" << info.workdir << "\n";
    std::cout << std::setw(20) << "Requested CPUs:" << info.ncpu << "\n";
    std::cout << std::setw(20) << "Requested GPUs:" << info.ngpu << "\n";
    
    if (!info.specific_cpus.empty()) {
        std::cout << std::setw(20) << "Specific CPUs:" << formatIntList(info.specific_cpus) << "\n";
    }
    if (!info.specific_gpus.empty()) {
        std::cout << std::setw(20) << "Specific GPUs:" << formatIntList(info.specific_gpus) << "\n";
    }
    
    std::cout << std::setw(20) << "Allocated CPUs:" << formatIntList(info.allocated_cpus) << "\n";
    std::cout << std::setw(20) << "Allocated GPUs:" << formatIntList(info.allocated_gpus) << "\n";
    
    if (!info.log_file.empty()) {
        std::cout << std::setw(20) << "Log file:" << info.log_file << "\n";
    }
    
    if (info.pid > 0) {
        std::cout << std::setw(20) << "PID:" << info.pid << "\n";
    }
    
    std::cout << std::setw(20) << "Submit time:" << info.submit_time << "\n";
    
    if (!info.start_time.empty()) {
        std::cout << std::setw(20) << "Start time:" << info.start_time << "\n";
    }
    
    if (!info.end_time.empty()) {
        std::cout << std::setw(20) << "End time:" << info.end_time << "\n";
    }
    
    if (info.duration_seconds > 0) {
        std::cout << std::setw(20) << "Duration:" << formatDuration(info.duration_seconds) 
                  << " (" << info.duration_seconds << "s)\n";
    }
    
    if (info.status == "completed" || info.status == "failed") {
        std::cout << std::setw(20) << "Exit code:" << info.exit_code << "\n";
    }
    
    return 0;
}

// Log command - show task log
int handleLog(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Error: Missing task ID\n";
        std::cerr << "Usage: myqueue log <id> [-n <lines>]\n";
        return 1;
    }
    
    uint64_t task_id;
    try {
        task_id = std::stoull(argv[2]);
    } catch (...) {
        std::cerr << "Error: Invalid task ID: " << argv[2] << "\n";
        return 1;
    }
    
    int tail_lines = 0;
    
    // Parse options
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-n" || arg == "--tail") && i + 1 < argc) {
            try {
                tail_lines = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid line count: " << argv[i] << "\n";
                return 1;
            }
        }
    }
    
    Config config = Config::fromArgs(0, nullptr);
    IPCClient client(config.socket_path);
    
    if (!client.connect()) {
        std::cerr << "Error: Cannot connect to server. Is the server running?\n";
        return 1;
    }
    
    auto response = client.getTaskLog(task_id, tail_lines);
    if (!response.has_value()) {
        std::cerr << "Error: Failed to get task log: " << client.lastError() << "\n";
        return 1;
    }
    
    const auto& log_resp = *response;
    
    if (!log_resp.found) {
        std::cerr << "Error: " << log_resp.error << "\n";
        return 1;
    }
    
    // Print log path header
    std::cerr << Color::gray() << "=== Log: " << log_resp.log_path << " ===" << Color::reset() << "\n";
    
    // Print log content
    std::cout << log_resp.content;
    
    // Ensure newline at end
    if (!log_resp.content.empty() && log_resp.content.back() != '\n') {
        std::cout << "\n";
    }
    
    return 0;
}

int run(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "server") {
        return handleServer(argc, argv);
    } else if (command == "stop") {
        return handleStop(argc, argv);
    } else if (command == "init") {
        return handleInit(argc, argv);
    } else if (command == "res") {
        return handleResource(argc, argv);
    } else if (command == "sb") {
        return handleSubmit(argc, argv);
    } else if (command == "sq") {
        return handleQueue(argc, argv);
    } else if (command == "del") {
        return handleDelete(argc, argv);
    } else if (command == "info") {
        return handleInfo(argc, argv);
    } else if (command == "log") {
        return handleLog(argc, argv);
    } else if (command == "-h" || command == "--help") {
        printUsage(argv[0]);
        return 0;
    } else if (command == "-v" || command == "--version") {
        printVersion();
        return 0;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }
}

} // namespace myqueue

int main(int argc, char* argv[]) {
    return myqueue::run(argc, argv);
}

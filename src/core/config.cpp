/**
 * @file config.cpp
 * @brief Implementation of Config class
 * 
 * Handles command-line argument parsing, JSON serialization,
 * and automatic path detection based on system environment.
 */

#include "myqueue/config.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace myqueue {

/**
 * @brief Expand ~ to home directory in path
 * @param path Path that may contain ~
 * @return Expanded path
 */
static std::string expandPath(const std::string& path) {
    if (path.empty()) {
        return path;
    }
    if (path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0') {
            return std::string(home) + path.substr(1);
        }
        // Fall back to getpwuid
        struct passwd* pw = getpwuid(getuid());
        if (pw != nullptr && pw->pw_dir != nullptr) {
            return std::string(pw->pw_dir) + path.substr(1);
        }
    }
    return path;
}

std::string Config::getHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "localhost";
}

std::string Config::getUsername() {
    // Try environment variable first
    const char* user = std::getenv("USER");
    if (user != nullptr && user[0] != '\0') {
        return std::string(user);
    }
    
    // Fall back to getpwuid
    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_name != nullptr) {
        return std::string(pw->pw_name);
    }
    
    return "unknown";
}

std::string Config::getHomeDir() {
    // Try environment variable first
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home);
    }
    
    // Fall back to getpwuid
    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_dir != nullptr) {
        return std::string(pw->pw_dir);
    }
    
    return "/tmp";
}

void Config::initDefaultPaths() {
    std::string hostname = getHostname();
    std::string username = getUsername();
    std::string home = getHomeDir();
    
    // Socket path: /tmp/myqueue_<username>.sock
    socket_path = "/tmp/myqueue_" + username + ".sock";
    
    // Data directory: ~/.myqueue/<hostname>/
    data_dir = home + "/.myqueue/" + hostname;
}

Config Config::fromArgs(int argc, char* argv[]) {
    Config config;
    
    // Initialize default paths first
    config.initDefaultPaths();
    
    // Helper function to parse comma-separated integers
    auto parseIntList = [](const std::string& str) -> std::vector<int> {
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
    };
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--log" && i + 1 < argc) {
            config.enable_logging = true;
            config.log_dir = expandPath(argv[++i]);
        } else if (arg == "--gpumemory" && i + 1 < argc) {
            try {
                config.gpu_memory_threshold_mb = std::stoul(argv[++i]);
            } catch (const std::exception&) {
                // Keep default value on parse error
            }
        } else if (arg == "--cpuusage" && i + 1 < argc) {
            try {
                config.cpu_util_threshold = std::stod(argv[++i]);
            } catch (const std::exception&) {
                // Keep default value on parse error
            }
        } else if (arg == "--joblog") {
            config.enable_job_log = true;
        } else if (arg == "--excpus" && i + 1 < argc) {
            config.excluded_cpus = parseIntList(argv[++i]);
        } else if (arg == "--exgpus" && i + 1 < argc) {
            config.excluded_gpus = parseIntList(argv[++i]);
        }
        // Other arguments are ignored (handled by CLI)
    }
    
    return config;
}

std::string Config::toJson() const {
    nlohmann::json j;
    
    // Resource thresholds
    j["gpu_memory_threshold_mb"] = gpu_memory_threshold_mb;
    j["cpu_util_threshold"] = cpu_util_threshold;
    j["cpu_check_duration_ms"] = cpu_check_duration_ms;
    
    // Scheduling parameters
    j["scheduling_interval_ms"] = scheduling_interval_ms;
    j["process_check_interval_ms"] = process_check_interval_ms;
    
    // System topology
    j["total_cpus"] = total_cpus;
    j["total_gpus"] = total_gpus;
    
    // Paths
    j["socket_path"] = socket_path;
    j["data_dir"] = data_dir;
    j["log_dir"] = log_dir;
    
    // Logging
    j["enable_logging"] = enable_logging;
    j["enable_job_log"] = enable_job_log;
    
    // Resource exclusion
    j["excluded_cpus"] = excluded_cpus;
    j["excluded_gpus"] = excluded_gpus;
    
    return j.dump(2);  // Pretty print with 2-space indent
}

Config Config::fromJson(const std::string& json) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        Config config;
        
        // Resource thresholds
        if (j.contains("gpu_memory_threshold_mb")) {
            config.gpu_memory_threshold_mb = j["gpu_memory_threshold_mb"].get<size_t>();
        }
        if (j.contains("cpu_util_threshold")) {
            config.cpu_util_threshold = j["cpu_util_threshold"].get<double>();
        }
        if (j.contains("cpu_check_duration_ms")) {
            config.cpu_check_duration_ms = j["cpu_check_duration_ms"].get<int>();
        }
        
        // Scheduling parameters
        if (j.contains("scheduling_interval_ms")) {
            config.scheduling_interval_ms = j["scheduling_interval_ms"].get<int>();
        }
        if (j.contains("process_check_interval_ms")) {
            config.process_check_interval_ms = j["process_check_interval_ms"].get<int>();
        }
        
        // System topology
        if (j.contains("total_cpus")) {
            config.total_cpus = j["total_cpus"].get<int>();
        }
        if (j.contains("total_gpus")) {
            config.total_gpus = j["total_gpus"].get<int>();
        }
        
        // Paths
        if (j.contains("socket_path")) {
            config.socket_path = j["socket_path"].get<std::string>();
        }
        if (j.contains("data_dir")) {
            config.data_dir = j["data_dir"].get<std::string>();
        }
        if (j.contains("log_dir")) {
            config.log_dir = j["log_dir"].get<std::string>();
        }
        
        // Logging
        if (j.contains("enable_logging")) {
            config.enable_logging = j["enable_logging"].get<bool>();
        }
        if (j.contains("enable_job_log")) {
            config.enable_job_log = j["enable_job_log"].get<bool>();
        }
        
        // Resource exclusion
        if (j.contains("excluded_cpus")) {
            config.excluded_cpus = j["excluded_cpus"].get<std::vector<int>>();
        }
        if (j.contains("excluded_gpus")) {
            config.excluded_gpus = j["excluded_gpus"].get<std::vector<int>>();
        }
        
        return config;
        
    } catch (const nlohmann::json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                               std::string("Config JSON parse error: ") + e.what());
    }
}

namespace {

/**
 * @brief Create directory recursively (like mkdir -p)
 * @param path Directory path to create
 * @return true if directory exists or was created successfully
 */
bool createDirectoryRecursive(const std::string& path) {
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

} // anonymous namespace

void Config::save() const {
    if (data_dir.empty()) {
        throw MyQueueException(ErrorCode::FILE_WRITE_ERROR, "Data directory not set");
    }
    
    // Create data directory if it doesn't exist
    if (!createDirectoryRecursive(data_dir)) {
        throw MyQueueException(ErrorCode::FILE_WRITE_ERROR,
                               "Failed to create data directory: " + data_dir);
    }
    
    std::string config_path = data_dir + "/config.json";
    std::ofstream file(config_path);
    
    if (!file.is_open()) {
        throw MyQueueException(ErrorCode::FILE_WRITE_ERROR,
                               "Failed to open config file for writing: " + config_path);
    }
    
    file << toJson();
    
    if (file.fail()) {
        throw MyQueueException(ErrorCode::FILE_WRITE_ERROR,
                               "Failed to write config file: " + config_path);
    }
}

Config Config::load(const std::string& data_dir) {
    std::string config_path = data_dir + "/config.json";
    std::ifstream file(config_path);
    
    if (!file.is_open()) {
        // File doesn't exist, return default config with paths set
        Config config;
        config.data_dir = data_dir;
        config.initDefaultPaths();
        config.data_dir = data_dir;  // Override with provided data_dir
        return config;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    if (file.fail() && !file.eof()) {
        throw MyQueueException(ErrorCode::FILE_READ_ERROR,
                               "Failed to read config file: " + config_path);
    }
    
    return fromJson(content);
}

bool Config::operator==(const Config& other) const {
    return gpu_memory_threshold_mb == other.gpu_memory_threshold_mb &&
           cpu_util_threshold == other.cpu_util_threshold &&
           cpu_check_duration_ms == other.cpu_check_duration_ms &&
           scheduling_interval_ms == other.scheduling_interval_ms &&
           process_check_interval_ms == other.process_check_interval_ms &&
           total_cpus == other.total_cpus &&
           total_gpus == other.total_gpus &&
           socket_path == other.socket_path &&
           data_dir == other.data_dir &&
           log_dir == other.log_dir &&
           enable_logging == other.enable_logging &&
           enable_job_log == other.enable_job_log;
}

} // namespace myqueue

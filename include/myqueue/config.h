/**
 * @file config.h
 * @brief Configuration management for myqueue
 * 
 * Defines the Config struct which holds all configuration parameters
 * for the myqueue system, including resource thresholds, paths, and
 * command-line argument parsing.
 */

#ifndef MYQUEUE_CONFIG_H
#define MYQUEUE_CONFIG_H

#include <cstddef>
#include <string>

namespace myqueue {

/**
 * @brief Configuration parameters for myqueue system
 * 
 * Contains all configurable parameters including:
 * - Resource thresholds (GPU memory, CPU utilization)
 * - Scheduling parameters (intervals)
 * - System topology (CPU/GPU counts)
 * - File paths (socket, data directory, logs)
 * 
 * Configuration can be loaded from command-line arguments using fromArgs().
 */
struct Config {
    // ========== Resource Thresholds ==========
    
    /// GPU memory threshold in MB. GPU is considered busy if memory usage exceeds this.
    /// Default: 2000 MB
    /// Can be set via --gpumemory command-line argument
    size_t gpu_memory_threshold_mb = 2000;
    
    /// CPU utilization threshold in percent. CPU is considered busy if utilization exceeds this.
    /// Default: 40.0%
    /// Can be set via --cpuusage command-line argument
    double cpu_util_threshold = 40.0;
    
    /// Duration in milliseconds to continuously monitor CPU utilization.
    /// CPU must stay below threshold for this duration to be considered available.
    /// Default: 3000 ms (3 seconds)
    int cpu_check_duration_ms = 3000;
    
    // ========== Scheduling Parameters ==========
    
    /// Interval in milliseconds between scheduling attempts.
    /// Default: 1000 ms (1 second)
    int scheduling_interval_ms = 1000;
    
    /// Interval in milliseconds between process status checks.
    /// Default: 500 ms
    int process_check_interval_ms = 500;
    
    // ========== System Topology ==========
    
    /// Total number of CPU cores in the system.
    /// Default: 64 (assumes dual-socket system with 32 cores each)
    int total_cpus = 64;
    
    /// Total number of GPU devices in the system.
    /// Default: 8
    int total_gpus = 8;
    
    // ========== Paths ==========
    
    /// Path to Unix domain socket for IPC.
    /// Format: /tmp/myqueue_<username>.sock
    std::string socket_path;
    
    /// Directory for persistent data storage.
    /// Format: ~/.myqueue/<hostname>/
    std::string data_dir;
    
    /// Directory for log files (empty if logging disabled).
    /// Set via --log command-line argument
    std::string log_dir;
    
    // ========== Logging ==========
    
    /// Whether logging is enabled.
    /// Set to true when --log argument is provided
    bool enable_logging = false;
    
    /// Whether job log output is enabled (output to workdir/job.log)
    /// Set via --joblog argument on server
    bool enable_job_log = false;
    
    // ========== Methods ==========
    
    /**
     * @brief Parse configuration from command-line arguments
     * 
     * Supported arguments:
     * - --log <path>: Enable logging and set log directory
     * - --gpumemory <MB>: Set GPU memory threshold
     * - --cpuusage <percent>: Set CPU utilization threshold
     * 
     * Also automatically sets:
     * - socket_path: /tmp/myqueue_<username>.sock
     * - data_dir: ~/.myqueue/<hostname>/
     * 
     * @param argc Argument count
     * @param argv Argument values
     * @return Configured Config object
     */
    static Config fromArgs(int argc, char* argv[]);
    
    /**
     * @brief Serialize configuration to JSON string
     * @return JSON string representation
     */
    std::string toJson() const;
    
    /**
     * @brief Deserialize configuration from JSON string
     * @param json JSON string to parse
     * @return Config object
     * @throws MyQueueException if JSON is invalid
     */
    static Config fromJson(const std::string& json);
    
    /**
     * @brief Save configuration to file
     * 
     * Saves to data_dir/config.json
     */
    void save() const;
    
    /**
     * @brief Load configuration from file
     * 
     * Loads from data_dir/config.json if it exists.
     * 
     * @param data_dir Directory containing config.json
     * @return Config object (default if file doesn't exist)
     */
    static Config load(const std::string& data_dir);
    
    /**
     * @brief Check equality of two configurations
     * @param other Config to compare with
     * @return true if all fields are equal
     */
    bool operator==(const Config& other) const;
    
    /**
     * @brief Check inequality of two configurations
     * @param other Config to compare with
     * @return true if any field differs
     */
    bool operator!=(const Config& other) const {
        return !(*this == other);
    }
    
private:
    /**
     * @brief Initialize default paths based on environment
     * 
     * Sets socket_path and data_dir based on username and hostname.
     */
    void initDefaultPaths();
    
    /**
     * @brief Get current hostname
     * @return Hostname string
     */
    static std::string getHostname();
    
    /**
     * @brief Get current username
     * @return Username string
     */
    static std::string getUsername();
    
    /**
     * @brief Get user's home directory
     * @return Home directory path
     */
    static std::string getHomeDir();
};

} // namespace myqueue

#endif // MYQUEUE_CONFIG_H

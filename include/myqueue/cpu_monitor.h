/**
 * @file cpu_monitor.h
 * @brief CPU monitoring and status detection for myqueue
 * 
 * Provides CPU status detection by reading /proc/stat to determine
 * CPU utilization and availability. Implements 3-second continuous
 * monitoring to ensure CPU is truly idle before allocation.
 */

#ifndef MYQUEUE_CPU_MONITOR_H
#define MYQUEUE_CPU_MONITOR_H

#include "myqueue/config.h"

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace myqueue {

/**
 * @brief Information about a single CPU core
 * 
 * Contains CPU identification, utilization, and availability status.
 */
struct CPUInfo {
    /// CPU core ID (0-63 typically)
    int core_id = 0;
    
    /// Current CPU utilization percentage (0-100)
    double utilization = 0.0;
    
    /// Whether CPU is allocated to a running task
    bool is_allocated = false;
    
    /// CPU affinity group (1 for cores 0-31, 2 for cores 32-63)
    int affinity_group = 1;
    
    /**
     * @brief Check equality of two CPUInfo objects
     * @param other CPUInfo to compare with
     * @return true if all fields are equal
     */
    bool operator==(const CPUInfo& other) const {
        return core_id == other.core_id &&
               utilization == other.utilization &&
               is_allocated == other.is_allocated &&
               affinity_group == other.affinity_group;
    }
    
    /**
     * @brief Check inequality of two CPUInfo objects
     * @param other CPUInfo to compare with
     * @return true if any field differs
     */
    bool operator!=(const CPUInfo& other) const {
        return !(*this == other);
    }
};

/**
 * @brief CPU time statistics from /proc/stat
 * 
 * Used to calculate CPU utilization between two time points.
 */
struct CPUStats {
    /// Total time spent in user mode
    unsigned long long user = 0;
    /// Total time spent in user mode with low priority (nice)
    unsigned long long nice = 0;
    /// Total time spent in system mode
    unsigned long long system = 0;
    /// Total time spent idle
    unsigned long long idle = 0;
    /// Total time spent waiting for I/O
    unsigned long long iowait = 0;
    /// Total time spent servicing interrupts
    unsigned long long irq = 0;
    /// Total time spent servicing softirqs
    unsigned long long softirq = 0;
    /// Total time spent in other states (steal, guest, etc.)
    unsigned long long steal = 0;
    
    /**
     * @brief Get total CPU time
     * @return Sum of all CPU time components
     */
    unsigned long long total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    
    /**
     * @brief Get total idle time
     * @return Sum of idle and iowait time
     */
    unsigned long long idleTime() const {
        return idle + iowait;
    }
    
    /**
     * @brief Get total active (non-idle) time
     * @return Total time minus idle time
     */
    unsigned long long activeTime() const {
        return total() - idleTime();
    }
};

/**
 * @brief CPU monitoring and status detection
 * 
 * Monitors CPU status by reading /proc/stat and calculating utilization.
 * Tracks which CPUs are allocated to tasks and determines availability
 * based on utilization threshold with 3-second continuous monitoring.
 * 
 * Thread-safe: All public methods are protected by mutex.
 */
class CPUMonitor {
public:
    /**
     * @brief Construct CPUMonitor with configuration
     * @param config Configuration containing CPU utilization threshold
     */
    explicit CPUMonitor(const Config& config);
    
    /**
     * @brief Construct CPUMonitor with custom parameters
     * @param util_threshold CPU utilization threshold percentage (default 40.0)
     * @param total_cpus Total number of CPU cores in system (default 64)
     * @param check_duration_ms Duration for continuous monitoring in ms (default 3000)
     * @param check_interval_ms Interval between checks in ms (default 500)
     */
    explicit CPUMonitor(double util_threshold = 40.0, 
                        int total_cpus = 64,
                        int check_duration_ms = 3000,
                        int check_interval_ms = 500);
    
    /**
     * @brief Get CPU utilization for a specific core
     * 
     * Reads /proc/stat twice with a small interval to calculate
     * the current CPU utilization percentage.
     * 
     * @param core_id CPU core ID to check
     * @return CPU utilization percentage (0-100), or -1 on error
     */
    double getCPUUtilization(int core_id);
    
    /**
     * @brief Check if a CPU core is available for allocation
     * 
     * A CPU is considered available if:
     * 1. It is not allocated to another task
     * 2. Its utilization stays below threshold for 3 seconds continuously
     * 
     * This method blocks for up to 3 seconds (configurable) while monitoring.
     * 
     * @param core_id CPU core ID to check
     * @return true if CPU is available for allocation
     */
    bool checkCPUAvailable(int core_id);
    
    /**
     * @brief Get list of available CPU core IDs in a specific affinity group
     * 
     * Returns CPUs that are:
     * - Not allocated to any task
     * - In the specified affinity group (or any group if 0)
     * 
     * Note: This does NOT perform the 3-second continuous check.
     * Use checkCPUAvailable() for full availability verification.
     * 
     * @param affinity_group 1 for cores 0-31, 2 for cores 32-63, 0 for all
     * @return Vector of potentially available CPU core IDs
     */
    std::vector<int> getAvailableCPUs(int affinity_group = 0);
    
    /**
     * @brief Get current status of all CPU cores
     * @return Vector of CPUInfo for all CPU cores
     */
    std::vector<CPUInfo> getCPUStatus();
    
    /**
     * @brief Mark CPUs as allocated to a task
     * @param cpu_ids CPU core IDs to mark as allocated
     */
    void allocateCPUs(const std::vector<int>& cpu_ids);
    
    /**
     * @brief Release CPUs from allocation
     * @param cpu_ids CPU core IDs to release
     */
    void releaseCPUs(const std::vector<int>& cpu_ids);
    
    /**
     * @brief Get set of currently allocated CPU IDs
     * @return Set of allocated CPU core IDs
     */
    std::set<int> getAllocatedCPUs() const;
    
    /**
     * @brief Get the CPU affinity group for a GPU
     * 
     * GPU 0-3 corresponds to affinity group 1 (CPU 0-31)
     * GPU 4-7 corresponds to affinity group 2 (CPU 32-63)
     * 
     * @param gpu_id GPU device ID
     * @return Affinity group (1 or 2)
     */
    static int getAffinityGroup(int gpu_id);
    
    /**
     * @brief Get the CPU range for an affinity group
     * @param affinity_group Affinity group (1 or 2)
     * @return Pair of (start_cpu, end_cpu) exclusive
     */
    static std::pair<int, int> getCPURangeForGroup(int affinity_group);
    
    /**
     * @brief Get the utilization threshold
     * @return Utilization threshold percentage
     */
    double getUtilThreshold() const { return util_threshold_; }
    
    /**
     * @brief Set the utilization threshold
     * @param threshold New threshold percentage
     */
    void setUtilThreshold(double threshold) { util_threshold_ = threshold; }
    
    /**
     * @brief Get total number of CPUs
     * @return Total CPU count
     */
    int getTotalCPUs() const { return total_cpus_; }
    
    /**
     * @brief Enable or disable mock mode for testing
     * 
     * In mock mode, /proc/stat is not read and mock data is used instead.
     * 
     * @param enable true to enable mock mode
     */
    void setMockMode(bool enable) { mock_mode_ = enable; }
    
    /**
     * @brief Set mock CPU utilization data for testing
     * @param mock_utils Map of core_id to utilization percentage
     */
    void setMockUtilization(const std::map<int, double>& mock_utils) { 
        mock_utilization_ = mock_utils; 
    }
    
    /**
     * @brief Set check duration for testing (shorter than 3 seconds)
     * @param duration_ms Check duration in milliseconds
     */
    void setCheckDuration(int duration_ms) { check_duration_ms_ = duration_ms; }
    
    /**
     * @brief Set check interval for testing
     * @param interval_ms Check interval in milliseconds
     */
    void setCheckInterval(int interval_ms) { check_interval_ms_ = interval_ms; }

private:
    /**
     * @brief Read CPU statistics from /proc/stat
     * @return Map of core_id to CPUStats
     */
    std::map<int, CPUStats> readProcStat();
    
    /**
     * @brief Parse a single CPU line from /proc/stat
     * @param line Line from /proc/stat (e.g., "cpu0 1234 5678 ...")
     * @return Pair of (core_id, CPUStats), core_id is -1 for aggregate "cpu"
     */
    std::pair<int, CPUStats> parseCPULine(const std::string& line);
    
    /**
     * @brief Calculate utilization between two stat readings
     * @param prev Previous CPU stats
     * @param curr Current CPU stats
     * @return Utilization percentage (0-100)
     */
    double calculateUtilization(const CPUStats& prev, const CPUStats& curr);
    
    /// CPU utilization threshold percentage
    double util_threshold_;
    
    /// Total number of CPU cores in system
    int total_cpus_;
    
    /// Duration for continuous monitoring in milliseconds
    int check_duration_ms_;
    
    /// Interval between checks in milliseconds
    int check_interval_ms_;
    
    /// Set of CPU IDs allocated to tasks
    std::set<int> allocated_cpus_;
    
    /// Mutex for thread safety
    mutable std::mutex mutex_;
    
    /// Mock mode flag for testing
    bool mock_mode_ = false;
    
    /// Mock utilization data for testing
    std::map<int, double> mock_utilization_;
};

} // namespace myqueue

#endif // MYQUEUE_CPU_MONITOR_H

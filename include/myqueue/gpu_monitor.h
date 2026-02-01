/**
 * @file gpu_monitor.h
 * @brief GPU monitoring and status detection for myqueue
 * 
 * Provides GPU status detection by calling nvidia-smi and parsing
 * its output to determine GPU memory usage and availability.
 */

#ifndef MYQUEUE_GPU_MONITOR_H
#define MYQUEUE_GPU_MONITOR_H

#include "myqueue/config.h"

#include <cstddef>
#include <set>
#include <string>
#include <vector>
#include <mutex>

namespace myqueue {

/**
 * @brief Information about a single GPU device
 * 
 * Contains GPU identification, memory usage, and availability status.
 */
struct GPUInfo {
    /// GPU device ID (0-7 typically)
    int device_id = 0;
    
    /// Current memory usage in MB
    size_t memory_used_mb = 0;
    
    /// Total memory capacity in MB
    size_t memory_total_mb = 0;
    
    /// Whether GPU is busy (memory usage exceeds threshold)
    bool is_busy = false;
    
    /// Whether GPU is allocated to a running task
    bool is_allocated = false;
    
    /**
     * @brief Check equality of two GPUInfo objects
     * @param other GPUInfo to compare with
     * @return true if all fields are equal
     */
    bool operator==(const GPUInfo& other) const {
        return device_id == other.device_id &&
               memory_used_mb == other.memory_used_mb &&
               memory_total_mb == other.memory_total_mb &&
               is_busy == other.is_busy &&
               is_allocated == other.is_allocated;
    }
    
    /**
     * @brief Check inequality of two GPUInfo objects
     * @param other GPUInfo to compare with
     * @return true if any field differs
     */
    bool operator!=(const GPUInfo& other) const {
        return !(*this == other);
    }
};

/**
 * @brief GPU monitoring and status detection
 * 
 * Monitors GPU status by calling nvidia-smi and parsing its output.
 * Tracks which GPUs are allocated to tasks and determines availability
 * based on memory usage threshold.
 * 
 * Thread-safe: All public methods are protected by mutex.
 */
class GPUMonitor {
public:
    /**
     * @brief Construct GPUMonitor with configuration
     * @param config Configuration containing GPU memory threshold
     */
    explicit GPUMonitor(const Config& config);
    
    /**
     * @brief Construct GPUMonitor with custom threshold
     * @param memory_threshold_mb GPU memory threshold in MB (default 2000)
     * @param total_gpus Total number of GPUs in system (default 8)
     */
    explicit GPUMonitor(size_t memory_threshold_mb = 2000, int total_gpus = 8);
    
    /**
     * @brief Query current GPU status from nvidia-smi
     * 
     * Calls nvidia-smi to get current memory usage for all GPUs.
     * Updates is_busy flag based on memory threshold.
     * 
     * @return Vector of GPUInfo for all detected GPUs
     */
    std::vector<GPUInfo> queryGPUs();
    
    /**
     * @brief Check if a specific GPU is busy
     * 
     * A GPU is considered busy if:
     * - It is allocated to a running task, OR
     * - Its memory usage exceeds the configured threshold
     * 
     * @param device_id GPU device ID to check
     * @return true if GPU is busy
     */
    bool isGPUBusy(int device_id);
    
    /**
     * @brief Get list of available GPU device IDs
     * 
     * Returns GPUs that are:
     * - Not allocated to any task
     * - Memory usage below threshold
     * 
     * GPUs are returned in order (0, 1, 2, ...) as per requirement 5.9.
     * 
     * @return Vector of available GPU device IDs
     */
    std::vector<int> getAvailableGPUs();
    
    /**
     * @brief Mark GPUs as allocated to a task
     * @param gpu_ids GPU device IDs to mark as allocated
     */
    void allocateGPUs(const std::vector<int>& gpu_ids);
    
    /**
     * @brief Release GPUs from allocation
     * @param gpu_ids GPU device IDs to release
     */
    void releaseGPUs(const std::vector<int>& gpu_ids);
    
    /**
     * @brief Get set of currently allocated GPU IDs
     * @return Set of allocated GPU device IDs
     */
    std::set<int> getAllocatedGPUs() const;
    
    /**
     * @brief Get the memory threshold
     * @return Memory threshold in MB
     */
    size_t getMemoryThreshold() const { return memory_threshold_mb_; }
    
    /**
     * @brief Set the memory threshold
     * @param threshold_mb New threshold in MB
     */
    void setMemoryThreshold(size_t threshold_mb) { memory_threshold_mb_ = threshold_mb; }
    
    /**
     * @brief Enable or disable mock mode for testing
     * 
     * In mock mode, nvidia-smi is not called and mock data is used instead.
     * 
     * @param enable true to enable mock mode
     */
    void setMockMode(bool enable) { mock_mode_ = enable; }
    
    /**
     * @brief Set mock GPU data for testing
     * @param mock_data Vector of GPUInfo to return in mock mode
     */
    void setMockData(const std::vector<GPUInfo>& mock_data) { mock_data_ = mock_data; }
    
    /**
     * @brief Check if nvidia-smi is available on the system
     * @return true if nvidia-smi can be executed
     */
    static bool isNvidiaSmiAvailable();

private:
    /**
     * @brief Execute nvidia-smi and get raw output
     * @return Raw output string from nvidia-smi, empty on failure
     */
    std::string executeNvidiaSmi();
    
    /**
     * @brief Parse nvidia-smi output into GPUInfo vector
     * @param output Raw nvidia-smi output
     * @return Vector of GPUInfo parsed from output
     */
    std::vector<GPUInfo> parseNvidiaSmiOutput(const std::string& output);
    
    /// GPU memory threshold in MB
    size_t memory_threshold_mb_;
    
    /// Total number of GPUs in system
    int total_gpus_;
    
    /// Set of GPU IDs allocated to tasks
    std::set<int> allocated_gpus_;
    
    /// Mutex for thread safety
    mutable std::mutex mutex_;
    
    /// Mock mode flag for testing
    bool mock_mode_ = false;
    
    /// Mock data for testing
    std::vector<GPUInfo> mock_data_;
};

} // namespace myqueue

#endif // MYQUEUE_GPU_MONITOR_H

/**
 * @file resource_monitor.h
 * @brief Unified resource monitoring for myqueue
 * 
 * Integrates GPU and CPU monitoring to provide a unified interface
 * for resource allocation and management. Implements CPU-GPU affinity
 * rules and resource allocation algorithms.
 */

#ifndef MYQUEUE_RESOURCE_MONITOR_H
#define MYQUEUE_RESOURCE_MONITOR_H

#include "myqueue/config.h"
#include "myqueue/gpu_monitor.h"
#include "myqueue/cpu_monitor.h"

#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace myqueue {

/**
 * @brief Result of a resource allocation attempt
 * 
 * Contains the allocated CPU and GPU IDs if allocation was successful.
 */
struct AllocationResult {
    /// Allocated CPU core IDs
    std::vector<int> cpus;
    
    /// Allocated GPU device IDs
    std::vector<int> gpus;
    
    /**
     * @brief Check if allocation was successful
     * @return true if at least one resource was allocated
     */
    bool isValid() const {
        return !cpus.empty() || !gpus.empty();
    }
};

/**
 * @brief Unified resource monitoring and allocation
 * 
 * Integrates GPUMonitor and CPUMonitor to provide:
 * - Unified resource status queries
 * - Resource allocation with CPU-GPU affinity enforcement
 * - Resource release management
 * 
 * Allocation Algorithm:
 * 1. Allocate GPUs first (in order 0-7 as per requirement 5.9)
 * 2. Determine CPU affinity group based on allocated GPUs
 * 3. Randomly select CPUs from the affinity group (with 3-second monitoring)
 * 
 * CPU-GPU Affinity Rules (requirement 5.8):
 * - GPU 0-3 corresponds to CPU 0-31 (affinity group 1)
 * - GPU 4-7 corresponds to CPU 32-63 (affinity group 2)
 * 
 * Thread-safe: All public methods are protected by mutex.
 */
class ResourceMonitor {
public:
    /**
     * @brief Construct ResourceMonitor with configuration
     * @param config Configuration containing resource thresholds
     */
    explicit ResourceMonitor(const Config& config);
    
    /**
     * @brief Construct ResourceMonitor with custom parameters
     * @param gpu_memory_threshold_mb GPU memory threshold in MB (default 2000)
     * @param cpu_util_threshold CPU utilization threshold percentage (default 40.0)
     * @param total_gpus Total number of GPUs (default 8)
     * @param total_cpus Total number of CPUs (default 64)
     * @param cpu_check_duration_ms Duration for CPU monitoring in ms (default 3000)
     */
    explicit ResourceMonitor(size_t gpu_memory_threshold_mb = 2000,
                             double cpu_util_threshold = 40.0,
                             int total_gpus = 8,
                             int total_cpus = 64,
                             int cpu_check_duration_ms = 3000);
    
    // ========== Resource Status Queries ==========
    
    /**
     * @brief Get current GPU status
     * @return Vector of GPUInfo for all GPUs
     */
    std::vector<GPUInfo> getGPUStatus();
    
    /**
     * @brief Get current CPU status
     * @return Vector of CPUInfo for all CPUs
     */
    std::vector<CPUInfo> getCPUStatus();
    
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
     * @brief Get list of available CPU core IDs in a specific affinity group
     * 
     * Returns CPUs that are not allocated to any task.
     * Note: This does NOT perform the 3-second continuous check.
     * 
     * @param affinity_group 1 for cores 0-31, 2 for cores 32-63, 0 for all
     * @return Vector of potentially available CPU core IDs
     */
    std::vector<int> getAvailableCPUs(int affinity_group = 0);
    
    // ========== Resource Allocation ==========
    
    /**
     * @brief Allocate resources for a task
     * 
     * Allocation algorithm:
     * 1. If specific_gpus provided, verify they are available
     * 2. Otherwise, allocate ngpu GPUs in order (0,1,2,3,4,5,6,7)
     * 3. Determine CPU affinity group based on allocated GPUs
     * 4. If specific_cpus provided, verify they are available
     * 5. Otherwise, randomly select ncpu CPUs from affinity group
     *    (each CPU must pass 3-second continuous monitoring)
     * 
     * @param ncpu Number of CPU cores to allocate
     * @param ngpu Number of GPU devices to allocate
     * @param specific_cpus Specific CPU IDs to allocate (empty for auto)
     * @param specific_gpus Specific GPU IDs to allocate (empty for auto)
     * @return AllocationResult if successful, nullopt if resources unavailable
     */
    std::optional<AllocationResult> allocate(
        int ncpu, int ngpu,
        const std::vector<int>& specific_cpus = {},
        const std::vector<int>& specific_gpus = {});
    
    /**
     * @brief Release allocated resources
     * 
     * Releases the specified CPUs and GPUs, making them available
     * for future allocations.
     * 
     * @param cpus CPU core IDs to release
     * @param gpus GPU device IDs to release
     */
    void release(const std::vector<int>& cpus, const std::vector<int>& gpus);
    
    // ========== Allocation Queries ==========
    
    /**
     * @brief Get set of currently allocated CPU IDs
     * @return Set of allocated CPU core IDs
     */
    std::set<int> getAllocatedCPUs() const;
    
    /**
     * @brief Get set of currently allocated GPU IDs
     * @return Set of allocated GPU device IDs
     */
    std::set<int> getAllocatedGPUs() const;
    
    // ========== Affinity Helpers ==========
    
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
    
    // ========== Configuration ==========
    
    /**
     * @brief Get the GPU memory threshold
     * @return Memory threshold in MB
     */
    size_t getGPUMemoryThreshold() const;
    
    /**
     * @brief Set the GPU memory threshold
     * @param threshold_mb New threshold in MB
     */
    void setGPUMemoryThreshold(size_t threshold_mb);
    
    /**
     * @brief Get the CPU utilization threshold
     * @return Utilization threshold percentage
     */
    double getCPUUtilThreshold() const;
    
    /**
     * @brief Set the CPU utilization threshold
     * @param threshold New threshold percentage
     */
    void setCPUUtilThreshold(double threshold);
    
    // ========== Testing Support ==========
    
    /**
     * @brief Enable or disable mock mode for testing
     * 
     * In mock mode, actual system queries are bypassed.
     * 
     * @param enable true to enable mock mode
     */
    void setMockMode(bool enable);
    
    /**
     * @brief Set mock GPU data for testing
     * @param mock_data Vector of GPUInfo to return in mock mode
     */
    void setMockGPUData(const std::vector<GPUInfo>& mock_data);
    
    /**
     * @brief Set mock CPU utilization data for testing
     * @param mock_utils Map of core_id to utilization percentage
     */
    void setMockCPUUtilization(const std::map<int, double>& mock_utils);
    
    /**
     * @brief Set CPU check duration for testing (shorter than 3 seconds)
     * @param duration_ms Check duration in milliseconds
     */
    void setCPUCheckDuration(int duration_ms);
    
    /**
     * @brief Set CPU check interval for testing
     * @param interval_ms Check interval in milliseconds
     */
    void setCPUCheckInterval(int interval_ms);
    
    /**
     * @brief Access the underlying GPU monitor (for testing)
     * @return Reference to GPUMonitor
     */
    GPUMonitor& gpuMonitor() { return gpu_monitor_; }
    
    /**
     * @brief Access the underlying CPU monitor (for testing)
     * @return Reference to CPUMonitor
     */
    CPUMonitor& cpuMonitor() { return cpu_monitor_; }

private:
    /**
     * @brief Allocate GPUs for a task
     * 
     * @param ngpu Number of GPUs to allocate
     * @param specific_gpus Specific GPU IDs (empty for auto)
     * @return Allocated GPU IDs, or empty if allocation failed
     */
    std::vector<int> allocateGPUs(int ngpu, const std::vector<int>& specific_gpus);
    
    /**
     * @brief Allocate CPUs for a task
     * 
     * @param ncpu Number of CPUs to allocate
     * @param affinity_group CPU affinity group (1 or 2, 0 for any)
     * @param specific_cpus Specific CPU IDs (empty for auto)
     * @return Allocated CPU IDs, or empty if allocation failed
     */
    std::vector<int> allocateCPUs(int ncpu, int affinity_group, 
                                   const std::vector<int>& specific_cpus);
    
    /**
     * @brief Determine affinity group from allocated GPUs
     * 
     * If all GPUs are in the same affinity group, returns that group.
     * If GPUs span both groups, returns 0 (any group).
     * If no GPUs, returns 0.
     * 
     * @param gpus Allocated GPU IDs
     * @return Affinity group (1, 2, or 0)
     */
    int determineAffinityGroup(const std::vector<int>& gpus) const;
    
    /// GPU monitor instance
    GPUMonitor gpu_monitor_;
    
    /// CPU monitor instance
    CPUMonitor cpu_monitor_;
    
    /// Random number generator for CPU selection
    mutable std::mt19937 rng_;
    
    /// Mutex for thread safety
    mutable std::mutex mutex_;
};

} // namespace myqueue

#endif // MYQUEUE_RESOURCE_MONITOR_H

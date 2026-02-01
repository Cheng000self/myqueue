/**
 * @file resource_monitor.cpp
 * @brief Implementation of unified resource monitoring
 * 
 * Implements resource allocation with CPU-GPU affinity enforcement.
 * GPU allocation follows sequential order (0-7), CPU allocation uses
 * random selection with 3-second continuous monitoring.
 */

#include "myqueue/resource_monitor.h"

#include <algorithm>
#include <chrono>

namespace myqueue {

ResourceMonitor::ResourceMonitor(const Config& config)
    : gpu_monitor_(config)
    , cpu_monitor_(config)
    , rng_(std::random_device{}()) {
}

ResourceMonitor::ResourceMonitor(size_t gpu_memory_threshold_mb,
                                 double cpu_util_threshold,
                                 int total_gpus,
                                 int total_cpus,
                                 int cpu_check_duration_ms)
    : gpu_monitor_(gpu_memory_threshold_mb, total_gpus)
    , cpu_monitor_(cpu_util_threshold, total_cpus, cpu_check_duration_ms, 500)
    , rng_(std::random_device{}()) {
}

// ============================================================================
// Resource Status Queries
// ============================================================================

std::vector<GPUInfo> ResourceMonitor::getGPUStatus() {
    return gpu_monitor_.queryGPUs();
}

std::vector<CPUInfo> ResourceMonitor::getCPUStatus() {
    return cpu_monitor_.getCPUStatus();
}

std::vector<int> ResourceMonitor::getAvailableGPUs() {
    return gpu_monitor_.getAvailableGPUs();
}

std::vector<int> ResourceMonitor::getAvailableCPUs(int affinity_group) {
    return cpu_monitor_.getAvailableCPUs(affinity_group);
}

// ============================================================================
// Resource Allocation
// ============================================================================

std::optional<AllocationResult> ResourceMonitor::allocate(
    int ncpu, int ngpu,
    const std::vector<int>& specific_cpus,
    const std::vector<int>& specific_gpus) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    AllocationResult result;
    
    // Step 1: Allocate GPUs first (in order 0-7 as per requirement 5.9)
    if (ngpu > 0 || !specific_gpus.empty()) {
        result.gpus = allocateGPUs(ngpu, specific_gpus);
        
        // If specific GPUs were requested but couldn't be allocated
        if (!specific_gpus.empty() && result.gpus.size() != specific_gpus.size()) {
            return std::nullopt;
        }
        
        // If auto allocation failed to get enough GPUs
        if (specific_gpus.empty() && static_cast<int>(result.gpus.size()) < ngpu) {
            return std::nullopt;
        }
    }
    
    // Step 2: Determine CPU affinity group based on allocated GPUs
    int affinity_group = determineAffinityGroup(result.gpus);
    
    // Step 3: Allocate CPUs (random selection with 3-second monitoring)
    if (ncpu > 0 || !specific_cpus.empty()) {
        result.cpus = allocateCPUs(ncpu, affinity_group, specific_cpus);
        
        // If specific CPUs were requested but couldn't be allocated
        if (!specific_cpus.empty() && result.cpus.size() != specific_cpus.size()) {
            // Rollback GPU allocation
            if (!result.gpus.empty()) {
                gpu_monitor_.releaseGPUs(result.gpus);
            }
            return std::nullopt;
        }
        
        // If auto allocation failed to get enough CPUs
        if (specific_cpus.empty() && static_cast<int>(result.cpus.size()) < ncpu) {
            // Rollback GPU allocation
            if (!result.gpus.empty()) {
                gpu_monitor_.releaseGPUs(result.gpus);
            }
            return std::nullopt;
        }
    }
    
    return result;
}

void ResourceMonitor::release(const std::vector<int>& cpus, 
                               const std::vector<int>& gpus) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cpus.empty()) {
        cpu_monitor_.releaseCPUs(cpus);
    }
    
    if (!gpus.empty()) {
        gpu_monitor_.releaseGPUs(gpus);
    }
}

// ============================================================================
// Allocation Queries
// ============================================================================

std::set<int> ResourceMonitor::getAllocatedCPUs() const {
    return cpu_monitor_.getAllocatedCPUs();
}

std::set<int> ResourceMonitor::getAllocatedGPUs() const {
    return gpu_monitor_.getAllocatedGPUs();
}

// ============================================================================
// Affinity Helpers
// ============================================================================

int ResourceMonitor::getAffinityGroup(int gpu_id) {
    return CPUMonitor::getAffinityGroup(gpu_id);
}

std::pair<int, int> ResourceMonitor::getCPURangeForGroup(int affinity_group) {
    return CPUMonitor::getCPURangeForGroup(affinity_group);
}

// ============================================================================
// Configuration
// ============================================================================

size_t ResourceMonitor::getGPUMemoryThreshold() const {
    return gpu_monitor_.getMemoryThreshold();
}

void ResourceMonitor::setGPUMemoryThreshold(size_t threshold_mb) {
    gpu_monitor_.setMemoryThreshold(threshold_mb);
}

double ResourceMonitor::getCPUUtilThreshold() const {
    return cpu_monitor_.getUtilThreshold();
}

void ResourceMonitor::setCPUUtilThreshold(double threshold) {
    cpu_monitor_.setUtilThreshold(threshold);
}

// ============================================================================
// Testing Support
// ============================================================================

void ResourceMonitor::setMockMode(bool enable) {
    gpu_monitor_.setMockMode(enable);
    cpu_monitor_.setMockMode(enable);
}

void ResourceMonitor::setMockGPUData(const std::vector<GPUInfo>& mock_data) {
    gpu_monitor_.setMockData(mock_data);
}

void ResourceMonitor::setMockCPUUtilization(const std::map<int, double>& mock_utils) {
    cpu_monitor_.setMockUtilization(mock_utils);
}

void ResourceMonitor::setCPUCheckDuration(int duration_ms) {
    cpu_monitor_.setCheckDuration(duration_ms);
}

void ResourceMonitor::setCPUCheckInterval(int interval_ms) {
    cpu_monitor_.setCheckInterval(interval_ms);
}

// ============================================================================
// Private Methods
// ============================================================================

std::vector<int> ResourceMonitor::allocateGPUs(int ngpu, 
                                                const std::vector<int>& specific_gpus) {
    std::vector<int> allocated;
    
    if (!specific_gpus.empty()) {
        // Check if any requested GPU is excluded
        for (int gpu_id : specific_gpus) {
            if (excluded_gpus_.count(gpu_id) > 0) {
                return {};  // Requested GPU is excluded
            }
        }
        
        // Verify specific GPUs are available
        for (int gpu_id : specific_gpus) {
            if (gpu_monitor_.isGPUBusy(gpu_id)) {
                return {};  // Requested GPU is not available
            }
        }
        
        // All requested GPUs are available, allocate them
        gpu_monitor_.allocateGPUs(specific_gpus);
        return specific_gpus;
    }
    
    // Auto allocation: check GPUs in order 0,1,2,3,4,5,6,7 (requirement 5.9)
    auto available = gpu_monitor_.getAvailableGPUs();
    
    for (int gpu_id : available) {
        // Skip excluded GPUs
        if (excluded_gpus_.count(gpu_id) > 0) {
            continue;
        }
        
        if (static_cast<int>(allocated.size()) >= ngpu) {
            break;
        }
        allocated.push_back(gpu_id);
    }
    
    if (static_cast<int>(allocated.size()) < ngpu) {
        return {};  // Not enough GPUs available
    }
    
    // Mark GPUs as allocated
    gpu_monitor_.allocateGPUs(allocated);
    
    return allocated;
}

std::vector<int> ResourceMonitor::allocateCPUs(int ncpu, int affinity_group,
                                                const std::vector<int>& specific_cpus) {
    std::vector<int> allocated;
    
    if (!specific_cpus.empty()) {
        // Check if any requested CPU is excluded
        for (int cpu_id : specific_cpus) {
            if (excluded_cpus_.count(cpu_id) > 0) {
                return {};  // Requested CPU is excluded
            }
        }
        
        // Verify specific CPUs are available (with 3-second monitoring)
        for (int cpu_id : specific_cpus) {
            if (!cpu_monitor_.checkCPUAvailable(cpu_id)) {
                return {};  // Requested CPU is not available
            }
        }
        
        // All requested CPUs are available, allocate them
        cpu_monitor_.allocateCPUs(specific_cpus);
        return specific_cpus;
    }
    
    // Auto allocation: get candidates from affinity group
    auto candidates = cpu_monitor_.getAvailableCPUs(affinity_group);
    
    // Filter out excluded CPUs
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                      [this](int cpu_id) { return excluded_cpus_.count(cpu_id) > 0; }),
        candidates.end());
    
    if (static_cast<int>(candidates.size()) < ncpu) {
        return {};  // Not enough candidate CPUs
    }
    
    // Randomly shuffle candidates (requirement 5.10)
    std::shuffle(candidates.begin(), candidates.end(), rng_);
    
    // Check each candidate with 3-second continuous monitoring
    for (int cpu_id : candidates) {
        if (static_cast<int>(allocated.size()) >= ncpu) {
            break;
        }
        
        // Check if CPU is still available (3-second monitoring)
        if (cpu_monitor_.checkCPUAvailable(cpu_id)) {
            allocated.push_back(cpu_id);
            // Mark as allocated immediately to prevent race conditions
            cpu_monitor_.allocateCPUs({cpu_id});
        }
    }
    
    if (static_cast<int>(allocated.size()) < ncpu) {
        // Not enough CPUs passed the monitoring check
        // Release any CPUs we did allocate
        if (!allocated.empty()) {
            cpu_monitor_.releaseCPUs(allocated);
        }
        return {};
    }
    
    return allocated;
}

int ResourceMonitor::determineAffinityGroup(const std::vector<int>& gpus) const {
    if (gpus.empty()) {
        return 0;  // No GPUs, any affinity group is fine
    }
    
    bool has_group1 = false;
    bool has_group2 = false;
    
    for (int gpu_id : gpus) {
        int group = CPUMonitor::getAffinityGroup(gpu_id);
        if (group == 1) {
            has_group1 = true;
        } else {
            has_group2 = true;
        }
    }
    
    if (has_group1 && has_group2) {
        // GPUs span both groups - this shouldn't happen in normal usage
        // but we handle it by allowing any CPU
        return 0;
    }
    
    return has_group1 ? 1 : 2;
}

void ResourceMonitor::setExcludedCPUs(const std::vector<int>& cpus) {
    std::lock_guard<std::mutex> lock(mutex_);
    excluded_cpus_.clear();
    excluded_cpus_.insert(cpus.begin(), cpus.end());
}

void ResourceMonitor::setExcludedGPUs(const std::vector<int>& gpus) {
    std::lock_guard<std::mutex> lock(mutex_);
    excluded_gpus_.clear();
    excluded_gpus_.insert(gpus.begin(), gpus.end());
}

std::set<int> ResourceMonitor::getExcludedCPUs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return excluded_cpus_;
}

std::set<int> ResourceMonitor::getExcludedGPUs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return excluded_gpus_;
}

} // namespace myqueue

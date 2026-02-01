/**
 * @file gpu_monitor.cpp
 * @brief Implementation of GPU monitoring functionality
 * 
 * Implements GPU status detection by calling nvidia-smi and parsing
 * its output to determine GPU memory usage and availability.
 */

#include "myqueue/gpu_monitor.h"
#include "myqueue/errors.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace myqueue {

GPUMonitor::GPUMonitor(const Config& config)
    : memory_threshold_mb_(config.gpu_memory_threshold_mb)
    , total_gpus_(config.total_gpus) {
}

GPUMonitor::GPUMonitor(size_t memory_threshold_mb, int total_gpus)
    : memory_threshold_mb_(memory_threshold_mb)
    , total_gpus_(total_gpus) {
}

bool GPUMonitor::isNvidiaSmiAvailable() {
    // Try to execute nvidia-smi with a simple query
    FILE* pipe = popen("nvidia-smi --query-gpu=index --format=csv,noheader,nounits 2>/dev/null", "r");
    if (pipe == nullptr) {
        return false;
    }
    
    // Read output to check if command succeeded
    std::array<char, 128> buffer;
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int status = pclose(pipe);
    
    // Check if command executed successfully (exit code 0)
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && !result.empty();
}

std::string GPUMonitor::executeNvidiaSmi() {
    // Execute nvidia-smi with query for index, memory.used, memory.total
    // Format: CSV without header and units
    // Example output: "0, 1234, 16384\n1, 567, 16384\n"
    const char* cmd = "nvidia-smi --query-gpu=index,memory.used,memory.total "
                      "--format=csv,noheader,nounits 2>/dev/null";
    
    FILE* pipe = popen(cmd, "r");
    if (pipe == nullptr) {
        return "";
    }
    
    std::string result;
    std::array<char, 256> buffer;
    
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    
    int status = pclose(pipe);
    
    // Check if command executed successfully
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return "";
    }
    
    return result;
}

std::vector<GPUInfo> GPUMonitor::parseNvidiaSmiOutput(const std::string& output) {
    std::vector<GPUInfo> gpus;
    
    if (output.empty()) {
        return gpus;
    }
    
    std::istringstream stream(output);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Parse line format: "index, memory.used, memory.total"
        // Example: "0, 1234, 16384"
        GPUInfo info;
        
        // Remove any leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos) {
            continue;
        }
        line = line.substr(start, end - start + 1);
        
        // Parse comma-separated values
        std::istringstream line_stream(line);
        std::string token;
        int field = 0;
        
        while (std::getline(line_stream, token, ',')) {
            // Trim whitespace from token
            size_t token_start = token.find_first_not_of(" \t");
            size_t token_end = token.find_last_not_of(" \t");
            if (token_start != std::string::npos && token_end != std::string::npos) {
                token = token.substr(token_start, token_end - token_start + 1);
            }
            
            try {
                switch (field) {
                    case 0:  // device_id
                        info.device_id = std::stoi(token);
                        break;
                    case 1:  // memory_used_mb
                        info.memory_used_mb = std::stoul(token);
                        break;
                    case 2:  // memory_total_mb
                        info.memory_total_mb = std::stoul(token);
                        break;
                }
            } catch (const std::exception&) {
                // Skip malformed lines
                break;
            }
            ++field;
        }
        
        // Only add if we parsed all three fields
        if (field >= 3) {
            // Determine if GPU is busy based on memory threshold
            info.is_busy = info.memory_used_mb > memory_threshold_mb_;
            
            // Check if GPU is allocated
            {
                std::lock_guard<std::mutex> lock(mutex_);
                info.is_allocated = allocated_gpus_.count(info.device_id) > 0;
            }
            
            gpus.push_back(info);
        }
    }
    
    return gpus;
}

std::vector<GPUInfo> GPUMonitor::queryGPUs() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // In mock mode, return mock data
    if (mock_mode_) {
        // Update is_busy and is_allocated flags based on current state
        std::vector<GPUInfo> result = mock_data_;
        for (auto& gpu : result) {
            gpu.is_busy = gpu.memory_used_mb > memory_threshold_mb_;
            gpu.is_allocated = allocated_gpus_.count(gpu.device_id) > 0;
        }
        return result;
    }
    
    // Execute nvidia-smi and parse output
    std::string output = executeNvidiaSmi();
    
    if (output.empty()) {
        // nvidia-smi failed, assume all GPUs are busy (safe default)
        // This follows the error handling strategy in design.md
        std::vector<GPUInfo> gpus;
        for (int i = 0; i < total_gpus_; ++i) {
            GPUInfo info;
            info.device_id = i;
            info.memory_used_mb = memory_threshold_mb_ + 1;  // Mark as busy
            info.memory_total_mb = 0;
            info.is_busy = true;
            info.is_allocated = allocated_gpus_.count(i) > 0;
            gpus.push_back(info);
        }
        return gpus;
    }
    
    // Need to release lock before calling parseNvidiaSmiOutput
    // since it also acquires the lock
    std::vector<GPUInfo> gpus;
    {
        // Temporarily release lock for parsing
        mutex_.unlock();
        gpus = parseNvidiaSmiOutput(output);
        mutex_.lock();
    }
    
    // Update allocation status
    for (auto& gpu : gpus) {
        gpu.is_allocated = allocated_gpus_.count(gpu.device_id) > 0;
    }
    
    return gpus;
}

bool GPUMonitor::isGPUBusy(int device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if allocated first
    if (allocated_gpus_.count(device_id) > 0) {
        return true;
    }
    
    // In mock mode, check mock data
    if (mock_mode_) {
        for (const auto& gpu : mock_data_) {
            if (gpu.device_id == device_id) {
                return gpu.memory_used_mb > memory_threshold_mb_;
            }
        }
        // GPU not found in mock data, assume busy
        return true;
    }
    
    // Query nvidia-smi for this specific GPU
    // Release lock temporarily
    mutex_.unlock();
    std::string output = executeNvidiaSmi();
    std::vector<GPUInfo> gpus = parseNvidiaSmiOutput(output);
    mutex_.lock();
    
    // Find the GPU in the results
    for (const auto& gpu : gpus) {
        if (gpu.device_id == device_id) {
            return gpu.is_busy;
        }
    }
    
    // GPU not found, assume busy (safe default)
    return true;
}

std::vector<int> GPUMonitor::getAvailableGPUs() {
    // Query current GPU status
    std::vector<GPUInfo> gpus = queryGPUs();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<int> available;
    
    // Check GPUs in order 0, 1, 2, ... as per requirement 5.9
    for (int i = 0; i < total_gpus_; ++i) {
        // Check if this GPU is in our query results
        bool found = false;
        bool is_available = false;
        
        for (const auto& gpu : gpus) {
            if (gpu.device_id == i) {
                found = true;
                // GPU is available if not busy and not allocated
                is_available = !gpu.is_busy && !gpu.is_allocated;
                break;
            }
        }
        
        // If GPU not found in results, check allocation only
        if (!found) {
            is_available = allocated_gpus_.count(i) == 0;
        }
        
        if (is_available) {
            available.push_back(i);
        }
    }
    
    return available;
}

void GPUMonitor::allocateGPUs(const std::vector<int>& gpu_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int id : gpu_ids) {
        allocated_gpus_.insert(id);
    }
}

void GPUMonitor::releaseGPUs(const std::vector<int>& gpu_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int id : gpu_ids) {
        allocated_gpus_.erase(id);
    }
}

std::set<int> GPUMonitor::getAllocatedGPUs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocated_gpus_;
}

} // namespace myqueue

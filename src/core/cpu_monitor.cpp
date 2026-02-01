/**
 * @file cpu_monitor.cpp
 * @brief Implementation of CPU monitoring functionality
 * 
 * Implements CPU status detection by reading /proc/stat and calculating
 * utilization. Provides 3-second continuous monitoring to ensure CPU
 * is truly idle before allocation.
 */

#include "myqueue/cpu_monitor.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace myqueue {

CPUMonitor::CPUMonitor(const Config& config)
    : util_threshold_(config.cpu_util_threshold)
    , total_cpus_(config.total_cpus)
    , check_duration_ms_(config.cpu_check_duration_ms)
    , check_interval_ms_(500) {
}

CPUMonitor::CPUMonitor(double util_threshold, int total_cpus,
                       int check_duration_ms, int check_interval_ms)
    : util_threshold_(util_threshold)
    , total_cpus_(total_cpus)
    , check_duration_ms_(check_duration_ms)
    , check_interval_ms_(check_interval_ms) {
}

int CPUMonitor::getAffinityGroup(int gpu_id) {
    // GPU 0-3 -> affinity group 1 (CPU 0-31)
    // GPU 4-7 -> affinity group 2 (CPU 32-63)
    return (gpu_id < 4) ? 1 : 2;
}

std::pair<int, int> CPUMonitor::getCPURangeForGroup(int affinity_group) {
    if (affinity_group == 1) {
        return {0, 32};   // CPU 0-31
    } else if (affinity_group == 2) {
        return {32, 64};  // CPU 32-63
    }
    // Default: all CPUs
    return {0, 64};
}

std::pair<int, CPUStats> CPUMonitor::parseCPULine(const std::string& line) {
    CPUStats stats;
    int core_id = -1;
    
    std::istringstream iss(line);
    std::string cpu_label;
    iss >> cpu_label;
    
    // Parse cpu label: "cpu" for aggregate, "cpu0", "cpu1", etc. for individual cores
    if (cpu_label == "cpu") {
        core_id = -1;  // Aggregate CPU stats
    } else if (cpu_label.substr(0, 3) == "cpu") {
        try {
            core_id = std::stoi(cpu_label.substr(3));
        } catch (const std::exception&) {
            return {-2, stats};  // Invalid format
        }
    } else {
        return {-2, stats};  // Not a CPU line
    }
    
    // Parse CPU time values
    // Format: user nice system idle iowait irq softirq steal [guest guest_nice]
    iss >> stats.user >> stats.nice >> stats.system >> stats.idle;
    
    // Optional fields (may not be present on all systems)
    if (!(iss >> stats.iowait)) stats.iowait = 0;
    if (!(iss >> stats.irq)) stats.irq = 0;
    if (!(iss >> stats.softirq)) stats.softirq = 0;
    if (!(iss >> stats.steal)) stats.steal = 0;
    
    return {core_id, stats};
}

std::map<int, CPUStats> CPUMonitor::readProcStat() {
    std::map<int, CPUStats> stats;
    
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return stats;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Only process lines starting with "cpu"
        if (line.substr(0, 3) != "cpu") {
            continue;
        }
        
        auto [core_id, cpu_stats] = parseCPULine(line);
        
        // Skip invalid lines and aggregate stats (core_id == -1)
        if (core_id >= 0) {
            stats[core_id] = cpu_stats;
        }
    }
    
    return stats;
}

double CPUMonitor::calculateUtilization(const CPUStats& prev, const CPUStats& curr) {
    unsigned long long total_diff = curr.total() - prev.total();
    unsigned long long idle_diff = curr.idleTime() - prev.idleTime();
    
    if (total_diff == 0) {
        return 0.0;
    }
    
    // Utilization = (total - idle) / total * 100
    double utilization = (static_cast<double>(total_diff - idle_diff) / 
                          static_cast<double>(total_diff)) * 100.0;
    
    // Clamp to valid range
    return std::max(0.0, std::min(100.0, utilization));
}

double CPUMonitor::getCPUUtilization(int core_id) {
    // In mock mode, return mock data
    if (mock_mode_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mock_utilization_.find(core_id);
        if (it != mock_utilization_.end()) {
            return it->second;
        }
        return 0.0;  // Default to 0% if not in mock data
    }
    
    // Read first sample
    auto stats1 = readProcStat();
    if (stats1.find(core_id) == stats1.end()) {
        return -1.0;  // Core not found
    }
    
    // Wait a short interval (100ms) to get meaningful utilization
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Read second sample
    auto stats2 = readProcStat();
    if (stats2.find(core_id) == stats2.end()) {
        return -1.0;  // Core not found
    }
    
    return calculateUtilization(stats1[core_id], stats2[core_id]);
}

bool CPUMonitor::checkCPUAvailable(int core_id) {
    // 1. Check if already allocated to another task
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (allocated_cpus_.count(core_id) > 0) {
            return false;
        }
    }
    
    // 2. Perform continuous monitoring for check_duration_ms_
    // Check every check_interval_ms_, all checks must pass
    int check_count = check_duration_ms_ / check_interval_ms_;
    if (check_count < 1) check_count = 1;
    
    for (int i = 0; i < check_count; ++i) {
        double util = getCPUUtilization(core_id);
        
        // If utilization is invalid or exceeds threshold, CPU is not available
        if (util < 0 || util >= util_threshold_) {
            return false;
        }
        
        // Sleep between checks (except for the last one)
        if (i < check_count - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms_));
        }
    }
    
    // All checks passed - CPU is available
    return true;
}

std::vector<int> CPUMonitor::getAvailableCPUs(int affinity_group) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<int> available;
    
    // Determine CPU range based on affinity group
    auto [start, end] = getCPURangeForGroup(affinity_group);
    
    // Limit to actual total CPUs
    end = std::min(end, total_cpus_);
    
    for (int i = start; i < end; ++i) {
        // Only include CPUs that are not allocated
        if (allocated_cpus_.count(i) == 0) {
            available.push_back(i);
        }
    }
    
    return available;
}

std::vector<CPUInfo> CPUMonitor::getCPUStatus() {
    std::vector<CPUInfo> cpus;
    
    // Get current utilization for all CPUs
    std::map<int, double> utilizations;
    
    if (mock_mode_) {
        std::lock_guard<std::mutex> lock(mutex_);
        utilizations = mock_utilization_;
    } else {
        // Read two samples to calculate utilization
        auto stats1 = readProcStat();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto stats2 = readProcStat();
        
        for (const auto& [core_id, curr_stats] : stats2) {
            auto it = stats1.find(core_id);
            if (it != stats1.end()) {
                utilizations[core_id] = calculateUtilization(it->second, curr_stats);
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (int i = 0; i < total_cpus_; ++i) {
        CPUInfo info;
        info.core_id = i;
        info.affinity_group = (i < 32) ? 1 : 2;
        info.is_allocated = allocated_cpus_.count(i) > 0;
        
        auto it = utilizations.find(i);
        if (it != utilizations.end()) {
            info.utilization = it->second;
        } else {
            info.utilization = 0.0;
        }
        
        cpus.push_back(info);
    }
    
    return cpus;
}

void CPUMonitor::allocateCPUs(const std::vector<int>& cpu_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int id : cpu_ids) {
        allocated_cpus_.insert(id);
    }
}

void CPUMonitor::releaseCPUs(const std::vector<int>& cpu_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int id : cpu_ids) {
        allocated_cpus_.erase(id);
    }
}

std::set<int> CPUMonitor::getAllocatedCPUs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocated_cpus_;
}

} // namespace myqueue

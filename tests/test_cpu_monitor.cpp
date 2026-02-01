/**
 * @file test_cpu_monitor.cpp
 * @brief Unit tests for CPU monitoring functionality
 * 
 * Tests CPU status detection, utilization calculation, affinity groups,
 * and allocation tracking. Uses mock mode to avoid dependency on actual
 * /proc/stat and to enable fast testing without 3-second waits.
 */

#include "myqueue/cpu_monitor.h"
#include "myqueue/config.h"

#include <gtest/gtest.h>
#include <map>
#include <set>
#include <vector>

namespace myqueue {
namespace testing {

/**
 * @brief Test fixture for CPUMonitor tests
 */
class CPUMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create monitor with default threshold (40%) and 64 CPUs
        // Use short check duration for faster tests
        monitor_ = std::make_unique<CPUMonitor>(40.0, 64, 100, 50);
        monitor_->setMockMode(true);
    }
    
    /**
     * @brief Create mock CPU utilization data
     * @param utils Map of core_id to utilization percentage
     */
    void setMockUtilization(const std::map<int, double>& utils) {
        monitor_->setMockUtilization(utils);
    }
    
    /**
     * @brief Create mock data with all CPUs at same utilization
     * @param util Utilization percentage for all CPUs
     */
    void setUniformUtilization(double util) {
        std::map<int, double> utils;
        for (int i = 0; i < 64; ++i) {
            utils[i] = util;
        }
        monitor_->setMockUtilization(utils);
    }
    
    std::unique_ptr<CPUMonitor> monitor_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(CPUMonitorTest, ConstructWithConfig) {
    Config config;
    config.cpu_util_threshold = 50.0;
    config.total_cpus = 32;
    config.cpu_check_duration_ms = 2000;
    
    CPUMonitor monitor(config);
    EXPECT_DOUBLE_EQ(monitor.getUtilThreshold(), 50.0);
    EXPECT_EQ(monitor.getTotalCPUs(), 32);
}

TEST_F(CPUMonitorTest, ConstructWithParameters) {
    CPUMonitor monitor(30.0, 48, 1500, 250);
    EXPECT_DOUBLE_EQ(monitor.getUtilThreshold(), 30.0);
    EXPECT_EQ(monitor.getTotalCPUs(), 48);
}

TEST_F(CPUMonitorTest, DefaultParameters) {
    CPUMonitor monitor;
    EXPECT_DOUBLE_EQ(monitor.getUtilThreshold(), 40.0);
    EXPECT_EQ(monitor.getTotalCPUs(), 64);
}

// ============================================================================
// CPU Affinity Group Tests
// ============================================================================

/**
 * @brief Test GPU to CPU affinity group mapping
 * 
 * Validates: Requirements 5.8
 * THE Resource_Monitor SHALL enforce CPU-GPU affinity rules:
 * GPU 0-3 with CPU 0-31, GPU 4-7 with CPU 32-63
 */
TEST_F(CPUMonitorTest, AffinityGroupForGPU0To3) {
    EXPECT_EQ(CPUMonitor::getAffinityGroup(0), 1);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(1), 1);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(2), 1);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(3), 1);
}

TEST_F(CPUMonitorTest, AffinityGroupForGPU4To7) {
    EXPECT_EQ(CPUMonitor::getAffinityGroup(4), 2);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(5), 2);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(6), 2);
    EXPECT_EQ(CPUMonitor::getAffinityGroup(7), 2);
}

/**
 * @brief Test CPU range for affinity groups
 */
TEST_F(CPUMonitorTest, CPURangeForGroup1) {
    auto [start, end] = CPUMonitor::getCPURangeForGroup(1);
    EXPECT_EQ(start, 0);
    EXPECT_EQ(end, 32);
}

TEST_F(CPUMonitorTest, CPURangeForGroup2) {
    auto [start, end] = CPUMonitor::getCPURangeForGroup(2);
    EXPECT_EQ(start, 32);
    EXPECT_EQ(end, 64);
}

TEST_F(CPUMonitorTest, CPURangeForInvalidGroup) {
    // Invalid group should return full range
    auto [start, end] = CPUMonitor::getCPURangeForGroup(0);
    EXPECT_EQ(start, 0);
    EXPECT_EQ(end, 64);
}

// ============================================================================
// CPU Utilization Tests
// ============================================================================

/**
 * @brief Test CPU utilization retrieval in mock mode
 */
TEST_F(CPUMonitorTest, GetCPUUtilizationMockMode) {
    std::map<int, double> utils = {{0, 25.5}, {1, 50.0}, {2, 75.0}};
    setMockUtilization(utils);
    
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(0), 25.5);
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(1), 50.0);
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(2), 75.0);
}

TEST_F(CPUMonitorTest, GetCPUUtilizationUnknownCore) {
    std::map<int, double> utils = {{0, 25.5}};
    setMockUtilization(utils);
    
    // Core not in mock data should return 0
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(99), 0.0);
}

// ============================================================================
// CPU Availability Tests
// ============================================================================

/**
 * @brief Test CPU availability when utilization is below threshold
 * 
 * Validates: Requirements 5.6, 5.7
 * WHEN checking CPU availability, THE Resource_Monitor SHALL monitor
 * CPU utilization for 3 seconds continuously.
 * IF CPU utilization stays below the configured threshold (default 40%)
 * for 3 seconds, THEN THE Resource_Monitor SHALL mark the CPU as available
 */
TEST_F(CPUMonitorTest, CPUAvailableWhenUtilizationBelowThreshold) {
    setUniformUtilization(20.0);  // 20% < 40% threshold
    
    EXPECT_TRUE(monitor_->checkCPUAvailable(0));
    EXPECT_TRUE(monitor_->checkCPUAvailable(31));
    EXPECT_TRUE(monitor_->checkCPUAvailable(32));
    EXPECT_TRUE(monitor_->checkCPUAvailable(63));
}

/**
 * @brief Test CPU unavailable when utilization exceeds threshold
 */
TEST_F(CPUMonitorTest, CPUUnavailableWhenUtilizationExceedsThreshold) {
    std::map<int, double> utils;
    for (int i = 0; i < 64; ++i) {
        utils[i] = (i == 5) ? 50.0 : 20.0;  // CPU 5 is busy
    }
    setMockUtilization(utils);
    
    EXPECT_TRUE(monitor_->checkCPUAvailable(0));
    EXPECT_FALSE(monitor_->checkCPUAvailable(5));  // 50% >= 40%
    EXPECT_TRUE(monitor_->checkCPUAvailable(10));
}

/**
 * @brief Test CPU unavailable when exactly at threshold
 * 
 * CPU is unavailable when utilization >= threshold
 */
TEST_F(CPUMonitorTest, CPUUnavailableAtExactThreshold) {
    std::map<int, double> utils = {{0, 40.0}};  // Exactly at threshold
    setMockUtilization(utils);
    
    EXPECT_FALSE(monitor_->checkCPUAvailable(0));
}

/**
 * @brief Test CPU available just below threshold
 */
TEST_F(CPUMonitorTest, CPUAvailableJustBelowThreshold) {
    std::map<int, double> utils = {{0, 39.9}};  // Just below threshold
    setMockUtilization(utils);
    
    EXPECT_TRUE(monitor_->checkCPUAvailable(0));
}

/**
 * @brief Test CPU unavailable when allocated
 * 
 * Validates: Requirements 5.5
 * THE Resource_Monitor SHALL track which CPUs are allocated to running tasks
 */
TEST_F(CPUMonitorTest, CPUUnavailableWhenAllocated) {
    setUniformUtilization(10.0);  // Low utilization
    
    // Allocate CPU 0
    monitor_->allocateCPUs({0});
    
    // CPU 0 should be unavailable due to allocation
    EXPECT_FALSE(monitor_->checkCPUAvailable(0));
    
    // Other CPUs should still be available
    EXPECT_TRUE(monitor_->checkCPUAvailable(1));
}

// ============================================================================
// CPU Allocation Tests
// ============================================================================

/**
 * @brief Test CPU allocation tracking
 */
TEST_F(CPUMonitorTest, AllocateCPUs) {
    monitor_->allocateCPUs({0, 5, 10});
    
    auto allocated = monitor_->getAllocatedCPUs();
    EXPECT_EQ(allocated.size(), 3);
    EXPECT_TRUE(allocated.count(0) > 0);
    EXPECT_TRUE(allocated.count(5) > 0);
    EXPECT_TRUE(allocated.count(10) > 0);
    EXPECT_TRUE(allocated.count(1) == 0);
}

/**
 * @brief Test CPU release
 */
TEST_F(CPUMonitorTest, ReleaseCPUs) {
    monitor_->allocateCPUs({0, 5, 10});
    monitor_->releaseCPUs({5});
    
    auto allocated = monitor_->getAllocatedCPUs();
    EXPECT_EQ(allocated.size(), 2);
    EXPECT_TRUE(allocated.count(0) > 0);
    EXPECT_TRUE(allocated.count(5) == 0);
    EXPECT_TRUE(allocated.count(10) > 0);
}

/**
 * @brief Test releasing non-allocated CPU (should be no-op)
 */
TEST_F(CPUMonitorTest, ReleaseNonAllocatedCPU) {
    monitor_->allocateCPUs({0});
    monitor_->releaseCPUs({99});  // Not allocated
    
    auto allocated = monitor_->getAllocatedCPUs();
    EXPECT_EQ(allocated.size(), 1);
    EXPECT_TRUE(allocated.count(0) > 0);
}

// ============================================================================
// Available CPUs Tests
// ============================================================================

/**
 * @brief Test getAvailableCPUs returns unallocated CPUs
 */
TEST_F(CPUMonitorTest, GetAvailableCPUsExcludesAllocated) {
    monitor_->allocateCPUs({0, 1, 32, 33});
    
    auto available = monitor_->getAvailableCPUs(0);  // All groups
    
    // Should have 60 available (64 - 4 allocated)
    EXPECT_EQ(available.size(), 60);
    
    // Allocated CPUs should not be in the list
    for (int cpu : available) {
        EXPECT_NE(cpu, 0);
        EXPECT_NE(cpu, 1);
        EXPECT_NE(cpu, 32);
        EXPECT_NE(cpu, 33);
    }
}

/**
 * @brief Test getAvailableCPUs with affinity group 1
 */
TEST_F(CPUMonitorTest, GetAvailableCPUsGroup1) {
    monitor_->allocateCPUs({0, 1});
    
    auto available = monitor_->getAvailableCPUs(1);  // Group 1: CPU 0-31
    
    // Should have 30 available (32 - 2 allocated)
    EXPECT_EQ(available.size(), 30);
    
    // All CPUs should be in range 0-31
    for (int cpu : available) {
        EXPECT_GE(cpu, 0);
        EXPECT_LT(cpu, 32);
    }
}

/**
 * @brief Test getAvailableCPUs with affinity group 2
 */
TEST_F(CPUMonitorTest, GetAvailableCPUsGroup2) {
    monitor_->allocateCPUs({32, 33, 34});
    
    auto available = monitor_->getAvailableCPUs(2);  // Group 2: CPU 32-63
    
    // Should have 29 available (32 - 3 allocated)
    EXPECT_EQ(available.size(), 29);
    
    // All CPUs should be in range 32-63
    for (int cpu : available) {
        EXPECT_GE(cpu, 32);
        EXPECT_LT(cpu, 64);
    }
}

/**
 * @brief Test getAvailableCPUs with no allocations
 */
TEST_F(CPUMonitorTest, GetAvailableCPUsNoAllocations) {
    auto available = monitor_->getAvailableCPUs(0);
    EXPECT_EQ(available.size(), 64);
}

/**
 * @brief Test getAvailableCPUs when all CPUs in group are allocated
 */
TEST_F(CPUMonitorTest, GetAvailableCPUsAllAllocated) {
    // Allocate all CPUs in group 1
    std::vector<int> group1_cpus;
    for (int i = 0; i < 32; ++i) {
        group1_cpus.push_back(i);
    }
    monitor_->allocateCPUs(group1_cpus);
    
    auto available = monitor_->getAvailableCPUs(1);
    EXPECT_TRUE(available.empty());
    
    // Group 2 should still have all CPUs available
    auto available2 = monitor_->getAvailableCPUs(2);
    EXPECT_EQ(available2.size(), 32);
}

// ============================================================================
// CPU Status Tests
// ============================================================================

/**
 * @brief Test getCPUStatus returns info for all CPUs
 */
TEST_F(CPUMonitorTest, GetCPUStatusReturnsAllCPUs) {
    setUniformUtilization(25.0);
    
    auto status = monitor_->getCPUStatus();
    
    EXPECT_EQ(status.size(), 64);
}

/**
 * @brief Test getCPUStatus includes correct affinity groups
 */
TEST_F(CPUMonitorTest, GetCPUStatusAffinityGroups) {
    setUniformUtilization(25.0);
    
    auto status = monitor_->getCPUStatus();
    
    // Check affinity groups
    for (const auto& cpu : status) {
        if (cpu.core_id < 32) {
            EXPECT_EQ(cpu.affinity_group, 1);
        } else {
            EXPECT_EQ(cpu.affinity_group, 2);
        }
    }
}

/**
 * @brief Test getCPUStatus includes allocation status
 */
TEST_F(CPUMonitorTest, GetCPUStatusAllocationStatus) {
    setUniformUtilization(25.0);
    monitor_->allocateCPUs({0, 32});
    
    auto status = monitor_->getCPUStatus();
    
    for (const auto& cpu : status) {
        if (cpu.core_id == 0 || cpu.core_id == 32) {
            EXPECT_TRUE(cpu.is_allocated);
        } else {
            EXPECT_FALSE(cpu.is_allocated);
        }
    }
}

/**
 * @brief Test getCPUStatus includes utilization
 */
TEST_F(CPUMonitorTest, GetCPUStatusUtilization) {
    std::map<int, double> utils = {{0, 10.0}, {1, 50.0}, {2, 90.0}};
    setMockUtilization(utils);
    
    auto status = monitor_->getCPUStatus();
    
    EXPECT_DOUBLE_EQ(status[0].utilization, 10.0);
    EXPECT_DOUBLE_EQ(status[1].utilization, 50.0);
    EXPECT_DOUBLE_EQ(status[2].utilization, 90.0);
}

// ============================================================================
// CPUInfo Equality Tests
// ============================================================================

TEST_F(CPUMonitorTest, CPUInfoEquality) {
    CPUInfo a, b;
    a.core_id = 0;
    a.utilization = 25.5;
    a.is_allocated = false;
    a.affinity_group = 1;
    
    b = a;
    EXPECT_EQ(a, b);
    
    b.utilization = 30.0;
    EXPECT_NE(a, b);
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @brief Test with zero utilization
 */
TEST_F(CPUMonitorTest, ZeroUtilization) {
    setUniformUtilization(0.0);
    
    EXPECT_TRUE(monitor_->checkCPUAvailable(0));
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(0), 0.0);
}

/**
 * @brief Test with 100% utilization
 */
TEST_F(CPUMonitorTest, FullUtilization) {
    setUniformUtilization(100.0);
    
    EXPECT_FALSE(monitor_->checkCPUAvailable(0));
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilization(0), 100.0);
}

/**
 * @brief Test threshold modification
 */
TEST_F(CPUMonitorTest, ModifyThreshold) {
    std::map<int, double> utils = {{0, 50.0}};
    setMockUtilization(utils);
    
    // With default 40% threshold, CPU should be unavailable
    EXPECT_FALSE(monitor_->checkCPUAvailable(0));
    
    // Increase threshold to 60%
    monitor_->setUtilThreshold(60.0);
    EXPECT_TRUE(monitor_->checkCPUAvailable(0));
}

/**
 * @brief Test with fewer CPUs than default
 */
TEST_F(CPUMonitorTest, FewerCPUsThanDefault) {
    CPUMonitor monitor(40.0, 8, 100, 50);  // Only 8 CPUs
    monitor.setMockMode(true);
    
    std::map<int, double> utils;
    for (int i = 0; i < 8; ++i) {
        utils[i] = 10.0;
    }
    monitor.setMockUtilization(utils);
    
    auto status = monitor.getCPUStatus();
    EXPECT_EQ(status.size(), 8);
    
    auto available = monitor.getAvailableCPUs(0);
    EXPECT_EQ(available.size(), 8);
}

/**
 * @brief Test multiple allocations and releases
 */
TEST_F(CPUMonitorTest, MultipleAllocationsAndReleases) {
    // First allocation
    monitor_->allocateCPUs({0, 1, 2});
    EXPECT_EQ(monitor_->getAllocatedCPUs().size(), 3);
    
    // Second allocation
    monitor_->allocateCPUs({3, 4});
    EXPECT_EQ(monitor_->getAllocatedCPUs().size(), 5);
    
    // Partial release
    monitor_->releaseCPUs({1, 3});
    EXPECT_EQ(monitor_->getAllocatedCPUs().size(), 3);
    
    // Full release
    monitor_->releaseCPUs({0, 2, 4});
    EXPECT_TRUE(monitor_->getAllocatedCPUs().empty());
}

/**
 * @brief Test duplicate allocation (should be idempotent)
 */
TEST_F(CPUMonitorTest, DuplicateAllocation) {
    monitor_->allocateCPUs({0, 1});
    monitor_->allocateCPUs({0, 2});  // 0 is already allocated
    
    auto allocated = monitor_->getAllocatedCPUs();
    EXPECT_EQ(allocated.size(), 3);  // 0, 1, 2
}

// ============================================================================
// Integration-like Tests
// ============================================================================

/**
 * @brief Test typical allocation workflow
 * 
 * Simulates the workflow of checking availability and allocating CPUs
 * for a task that needs 4 CPUs in affinity group 1.
 */
TEST_F(CPUMonitorTest, TypicalAllocationWorkflow) {
    // Set up: some CPUs are busy, some are idle
    std::map<int, double> utils;
    for (int i = 0; i < 64; ++i) {
        // CPUs 0-3 are busy, rest are idle
        utils[i] = (i < 4) ? 80.0 : 10.0;
    }
    setMockUtilization(utils);
    
    // Get available CPUs in group 1
    auto available = monitor_->getAvailableCPUs(1);
    EXPECT_EQ(available.size(), 32);  // All unallocated
    
    // Check availability (with continuous monitoring)
    std::vector<int> to_allocate;
    for (int cpu : available) {
        if (to_allocate.size() >= 4) break;
        if (monitor_->checkCPUAvailable(cpu)) {
            to_allocate.push_back(cpu);
        }
    }
    
    // Should find 4 available CPUs (4-7 or later, since 0-3 are busy)
    EXPECT_EQ(to_allocate.size(), 4);
    for (int cpu : to_allocate) {
        EXPECT_GE(cpu, 4);  // Should not include busy CPUs 0-3
    }
    
    // Allocate the CPUs
    monitor_->allocateCPUs(to_allocate);
    
    // Verify allocation
    auto allocated = monitor_->getAllocatedCPUs();
    EXPECT_EQ(allocated.size(), 4);
    
    // These CPUs should now be unavailable
    for (int cpu : to_allocate) {
        EXPECT_FALSE(monitor_->checkCPUAvailable(cpu));
    }
    
    // Release and verify
    monitor_->releaseCPUs(to_allocate);
    EXPECT_TRUE(monitor_->getAllocatedCPUs().empty());
}

} // namespace testing
} // namespace myqueue

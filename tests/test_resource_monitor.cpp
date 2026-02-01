/**
 * @file test_resource_monitor.cpp
 * @brief Unit tests for unified resource monitoring functionality
 * 
 * Tests resource allocation, CPU-GPU affinity enforcement, and resource release.
 * Uses mock mode to avoid dependency on actual nvidia-smi and /proc/stat.
 */

#include "myqueue/resource_monitor.h"
#include "myqueue/config.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace myqueue {
namespace testing {

/**
 * @brief Test fixture for ResourceMonitor tests
 */
class ResourceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create monitor with default thresholds
        // Use short check duration for faster tests
        monitor_ = std::make_unique<ResourceMonitor>(
            2000,   // GPU memory threshold
            40.0,   // CPU util threshold
            8,      // total GPUs
            64,     // total CPUs
            100     // CPU check duration (short for testing)
        );
        monitor_->setMockMode(true);
        monitor_->setCPUCheckInterval(50);
    }

    /**
     * @brief Create mock GPU data with specified memory usage
     * @param memory_values Vector of memory usage values for each GPU
     * @return Vector of GPUInfo with the specified memory usage
     */
    std::vector<GPUInfo> createMockGPUData(const std::vector<size_t>& memory_values) {
        std::vector<GPUInfo> gpus;
        for (size_t i = 0; i < memory_values.size(); ++i) {
            GPUInfo info;
            info.device_id = static_cast<int>(i);
            info.memory_used_mb = memory_values[i];
            info.memory_total_mb = 16384;
            info.is_busy = false;
            info.is_allocated = false;
            gpus.push_back(info);
        }
        return gpus;
    }
    
    /**
     * @brief Set all CPUs to same utilization
     * @param util Utilization percentage for all CPUs
     */
    void setUniformCPUUtilization(double util) {
        std::map<int, double> utils;
        for (int i = 0; i < 64; ++i) {
            utils[i] = util;
        }
        monitor_->setMockCPUUtilization(utils);
    }
    
    std::unique_ptr<ResourceMonitor> monitor_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(ResourceMonitorTest, ConstructWithConfig) {
    Config config;
    config.gpu_memory_threshold_mb = 3000;
    config.cpu_util_threshold = 50.0;
    config.total_gpus = 4;
    config.total_cpus = 32;
    
    ResourceMonitor monitor(config);
    EXPECT_EQ(monitor.getGPUMemoryThreshold(), 3000);
    EXPECT_DOUBLE_EQ(monitor.getCPUUtilThreshold(), 50.0);
}

TEST_F(ResourceMonitorTest, ConstructWithParameters) {
    ResourceMonitor monitor(1500, 30.0, 4, 32, 2000);
    EXPECT_EQ(monitor.getGPUMemoryThreshold(), 1500);
    EXPECT_DOUBLE_EQ(monitor.getCPUUtilThreshold(), 30.0);
}

TEST_F(ResourceMonitorTest, DefaultParameters) {
    ResourceMonitor monitor;
    EXPECT_EQ(monitor.getGPUMemoryThreshold(), 2000);
    EXPECT_DOUBLE_EQ(monitor.getCPUUtilThreshold(), 40.0);
}

// ============================================================================
// Affinity Group Tests
// ============================================================================

/**
 * @brief Test GPU to CPU affinity group mapping
 * 
 * Validates: Requirements 5.8
 * THE Resource_Monitor SHALL enforce CPU-GPU affinity rules:
 * GPU 0-3 with CPU 0-31, GPU 4-7 with CPU 32-63
 */
TEST_F(ResourceMonitorTest, AffinityGroupForGPU0To3) {
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(0), 1);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(1), 1);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(2), 1);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(3), 1);
}

TEST_F(ResourceMonitorTest, AffinityGroupForGPU4To7) {
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(4), 2);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(5), 2);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(6), 2);
    EXPECT_EQ(ResourceMonitor::getAffinityGroup(7), 2);
}

TEST_F(ResourceMonitorTest, CPURangeForGroup1) {
    auto [start, end] = ResourceMonitor::getCPURangeForGroup(1);
    EXPECT_EQ(start, 0);
    EXPECT_EQ(end, 32);
}

TEST_F(ResourceMonitorTest, CPURangeForGroup2) {
    auto [start, end] = ResourceMonitor::getCPURangeForGroup(2);
    EXPECT_EQ(start, 32);
    EXPECT_EQ(end, 64);
}

// ============================================================================
// Basic Allocation Tests
// ============================================================================

/**
 * @brief Test basic GPU allocation
 * 
 * Validates: Requirements 5.9
 * WHEN allocating resources, THE Scheduler SHALL check GPUs in order (0,1,2,3,4,5,6,7)
 */
TEST_F(ResourceMonitorTest, AllocateGPUsInOrder) {
    // All GPUs available with low memory
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 2 GPUs
    auto result = monitor_->allocate(0, 2, {}, {});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 2);
    // Should allocate GPUs 0 and 1 (in order)
    EXPECT_EQ(result->gpus[0], 0);
    EXPECT_EQ(result->gpus[1], 1);
}

/**
 * @brief Test GPU allocation skips busy GPUs
 */
TEST_F(ResourceMonitorTest, AllocateGPUsSkipsBusy) {
    // GPU 0 and 1 are busy
    auto mock_gpus = createMockGPUData({3000, 3000, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 2 GPUs
    auto result = monitor_->allocate(0, 2, {}, {});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 2);
    // Should allocate GPUs 2 and 3 (skipping busy 0 and 1)
    EXPECT_EQ(result->gpus[0], 2);
    EXPECT_EQ(result->gpus[1], 3);
}

/**
 * @brief Test allocation fails when not enough GPUs available
 */
TEST_F(ResourceMonitorTest, AllocateGPUsFailsWhenNotEnough) {
    // Only 2 GPUs available
    auto mock_gpus = createMockGPUData({100, 100, 3000, 3000, 3000, 3000, 3000, 3000});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Try to allocate 4 GPUs
    auto result = monitor_->allocate(0, 4, {}, {});
    
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test specific GPU allocation
 */
TEST_F(ResourceMonitorTest, AllocateSpecificGPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Request specific GPUs 2 and 5
    auto result = monitor_->allocate(0, 2, {}, {2, 5});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 2);
    EXPECT_EQ(result->gpus[0], 2);
    EXPECT_EQ(result->gpus[1], 5);
}

/**
 * @brief Test specific GPU allocation fails when GPU is busy
 */
TEST_F(ResourceMonitorTest, AllocateSpecificGPUsFailsWhenBusy) {
    // GPU 2 is busy
    auto mock_gpus = createMockGPUData({100, 100, 3000, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Request specific GPUs including busy GPU 2
    auto result = monitor_->allocate(0, 2, {}, {2, 5});
    
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// CPU Allocation Tests
// ============================================================================

/**
 * @brief Test basic CPU allocation
 */
TEST_F(ResourceMonitorTest, AllocateCPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 4 CPUs (no GPU, so any affinity group)
    auto result = monitor_->allocate(4, 0, {}, {});
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->cpus.size(), 4);
    EXPECT_TRUE(result->gpus.empty());
}

/**
 * @brief Test CPU allocation fails when utilization is high
 */
TEST_F(ResourceMonitorTest, AllocateCPUsFailsWhenBusy) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(80.0);  // All CPUs busy
    
    // Try to allocate CPUs
    auto result = monitor_->allocate(4, 0, {}, {});
    
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test specific CPU allocation
 */
TEST_F(ResourceMonitorTest, AllocateSpecificCPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Request specific CPUs 5, 10, 15
    auto result = monitor_->allocate(3, 0, {5, 10, 15}, {});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->cpus.size(), 3);
    EXPECT_EQ(result->cpus[0], 5);
    EXPECT_EQ(result->cpus[1], 10);
    EXPECT_EQ(result->cpus[2], 15);
}

/**
 * @brief Test specific CPU allocation fails when CPU is busy
 */
TEST_F(ResourceMonitorTest, AllocateSpecificCPUsFailsWhenBusy) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    
    // CPU 10 is busy
    std::map<int, double> utils;
    for (int i = 0; i < 64; ++i) {
        utils[i] = (i == 10) ? 80.0 : 10.0;
    }
    monitor_->setMockCPUUtilization(utils);
    
    // Request specific CPUs including busy CPU 10
    auto result = monitor_->allocate(3, 0, {5, 10, 15}, {});
    
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// CPU-GPU Affinity Tests
// ============================================================================

/**
 * @brief Test CPU-GPU affinity enforcement for GPU 0-3
 * 
 * Validates: Requirements 4.2, 5.8
 * WHEN allocating resources, THE Scheduler SHALL respect CPU-GPU affinity
 * (cpu0-31 with gpu0-3, cpu32-63 with gpu4-7)
 */
TEST_F(ResourceMonitorTest, AffinityEnforcementGroup1) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate GPU 0 (group 1) and 4 CPUs
    auto result = monitor_->allocate(4, 1, {}, {});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 1);
    EXPECT_EQ(result->gpus[0], 0);  // GPU 0 is in group 1
    
    // All CPUs should be in range 0-31 (group 1)
    for (int cpu : result->cpus) {
        EXPECT_GE(cpu, 0);
        EXPECT_LT(cpu, 32);
    }
}

/**
 * @brief Test CPU-GPU affinity enforcement for GPU 4-7
 */
TEST_F(ResourceMonitorTest, AffinityEnforcementGroup2) {
    // Make GPUs 0-3 busy so we get GPUs from group 2
    auto mock_gpus = createMockGPUData({3000, 3000, 3000, 3000, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 1 GPU and 4 CPUs
    auto result = monitor_->allocate(4, 1, {}, {});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 1);
    EXPECT_EQ(result->gpus[0], 4);  // GPU 4 is in group 2
    
    // All CPUs should be in range 32-63 (group 2)
    for (int cpu : result->cpus) {
        EXPECT_GE(cpu, 32);
        EXPECT_LT(cpu, 64);
    }
}

/**
 * @brief Test allocation with specific GPU enforces correct affinity
 */
TEST_F(ResourceMonitorTest, SpecificGPUEnforcesAffinity) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Request GPU 5 (group 2) and 4 CPUs
    auto result = monitor_->allocate(4, 1, {}, {5});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->gpus.size(), 1);
    EXPECT_EQ(result->gpus[0], 5);
    
    // All CPUs should be in range 32-63 (group 2)
    for (int cpu : result->cpus) {
        EXPECT_GE(cpu, 32);
        EXPECT_LT(cpu, 64);
    }
}

// ============================================================================
// Resource Release Tests
// ============================================================================

/**
 * @brief Test resource release makes resources available again
 * 
 * Validates: Requirements 4.11
 * WHEN resources are allocated to a task, THE Server SHALL mark those CPUs
 * and GPUs as busy until task completion or deletion
 */
TEST_F(ResourceMonitorTest, ReleaseResourcesMakesThemAvailable) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // First allocation
    auto result1 = monitor_->allocate(2, 2, {}, {});
    ASSERT_TRUE(result1.has_value());
    
    // Verify resources are allocated
    auto allocated_cpus = monitor_->getAllocatedCPUs();
    auto allocated_gpus = monitor_->getAllocatedGPUs();
    EXPECT_EQ(allocated_cpus.size(), 2);
    EXPECT_EQ(allocated_gpus.size(), 2);
    
    // Release resources
    monitor_->release(result1->cpus, result1->gpus);
    
    // Verify resources are released
    allocated_cpus = monitor_->getAllocatedCPUs();
    allocated_gpus = monitor_->getAllocatedGPUs();
    EXPECT_TRUE(allocated_cpus.empty());
    EXPECT_TRUE(allocated_gpus.empty());
    
    // Should be able to allocate again
    auto result2 = monitor_->allocate(2, 2, {}, {});
    ASSERT_TRUE(result2.has_value());
}

/**
 * @brief Test partial release
 */
TEST_F(ResourceMonitorTest, PartialRelease) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate resources
    auto result = monitor_->allocate(4, 2, {}, {});
    ASSERT_TRUE(result.has_value());
    
    // Release only some resources
    std::vector<int> cpus_to_release = {result->cpus[0], result->cpus[1]};
    std::vector<int> gpus_to_release = {result->gpus[0]};
    monitor_->release(cpus_to_release, gpus_to_release);
    
    // Verify partial release
    auto allocated_cpus = monitor_->getAllocatedCPUs();
    auto allocated_gpus = monitor_->getAllocatedGPUs();
    EXPECT_EQ(allocated_cpus.size(), 2);
    EXPECT_EQ(allocated_gpus.size(), 1);
}

// ============================================================================
// Combined Allocation Tests
// ============================================================================

/**
 * @brief Test combined CPU and GPU allocation
 * 
 * Validates: Requirements 2.2, 2.3
 * WHERE a user specifies --ncpu N, THE Scheduler SHALL allocate N CPU cores
 * WHERE a user specifies --ngpu N, THE Scheduler SHALL allocate N GPU devices
 */
TEST_F(ResourceMonitorTest, CombinedAllocation) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 4 CPUs and 2 GPUs
    auto result = monitor_->allocate(4, 2, {}, {});
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->cpus.size(), 4);
    EXPECT_EQ(result->gpus.size(), 2);
}

/**
 * @brief Test allocation with specific CPUs and GPUs
 * 
 * Validates: Requirements 2.5, 2.6
 * WHERE a user specifies --cpus "x,y,z", THE Scheduler SHALL allocate exactly those CPU cores
 * WHERE a user specifies --gpus "x,y,z", THE Scheduler SHALL allocate exactly those GPU devices
 */
TEST_F(ResourceMonitorTest, SpecificCPUsAndGPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Request specific CPUs and GPUs
    auto result = monitor_->allocate(3, 2, {0, 1, 2}, {0, 1});
    
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->cpus.size(), 3);
    ASSERT_EQ(result->gpus.size(), 2);
    
    EXPECT_EQ(result->cpus[0], 0);
    EXPECT_EQ(result->cpus[1], 1);
    EXPECT_EQ(result->cpus[2], 2);
    EXPECT_EQ(result->gpus[0], 0);
    EXPECT_EQ(result->gpus[1], 1);
}

/**
 * @brief Test multiple sequential allocations
 */
TEST_F(ResourceMonitorTest, MultipleSequentialAllocations) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // First allocation
    auto result1 = monitor_->allocate(2, 1, {}, {});
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->gpus[0], 0);  // First available GPU
    
    // Second allocation
    auto result2 = monitor_->allocate(2, 1, {}, {});
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->gpus[0], 1);  // Next available GPU
    
    // Third allocation
    auto result3 = monitor_->allocate(2, 1, {}, {});
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3->gpus[0], 2);  // Next available GPU
    
    // Verify all resources are allocated
    auto allocated_gpus = monitor_->getAllocatedGPUs();
    EXPECT_EQ(allocated_gpus.size(), 3);
}

// ============================================================================
// CPU Exclusivity Tests
// ============================================================================

/**
 * @brief Test that allocated CPUs cannot be allocated again
 * 
 * Validates: Requirements 4.3, 5.5
 * WHEN allocating CPUs, THE Scheduler SHALL verify the CPU is not allocated to other tasks
 */
TEST_F(ResourceMonitorTest, CPUExclusivity) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // First allocation with specific CPUs
    auto result1 = monitor_->allocate(4, 0, {0, 1, 2, 3}, {});
    ASSERT_TRUE(result1.has_value());
    
    // Try to allocate same CPUs again
    auto result2 = monitor_->allocate(4, 0, {0, 1, 2, 3}, {});
    EXPECT_FALSE(result2.has_value());
    
    // Try to allocate overlapping CPUs
    auto result3 = monitor_->allocate(4, 0, {2, 3, 4, 5}, {});
    EXPECT_FALSE(result3.has_value());
    
    // Non-overlapping CPUs should work
    auto result4 = monitor_->allocate(4, 0, {4, 5, 6, 7}, {});
    ASSERT_TRUE(result4.has_value());
}

/**
 * @brief Test that allocated GPUs cannot be allocated again
 */
TEST_F(ResourceMonitorTest, GPUExclusivity) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // First allocation with specific GPUs
    auto result1 = monitor_->allocate(0, 2, {}, {0, 1});
    ASSERT_TRUE(result1.has_value());
    
    // Try to allocate same GPUs again
    auto result2 = monitor_->allocate(0, 2, {}, {0, 1});
    EXPECT_FALSE(result2.has_value());
    
    // Try to allocate overlapping GPUs
    auto result3 = monitor_->allocate(0, 2, {}, {1, 2});
    EXPECT_FALSE(result3.has_value());
    
    // Non-overlapping GPUs should work
    auto result4 = monitor_->allocate(0, 2, {}, {2, 3});
    ASSERT_TRUE(result4.has_value());
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @brief Test allocation with zero resources
 */
TEST_F(ResourceMonitorTest, AllocateZeroResources) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Allocate 0 CPUs and 0 GPUs
    auto result = monitor_->allocate(0, 0, {}, {});
    
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->cpus.empty());
    EXPECT_TRUE(result->gpus.empty());
}

/**
 * @brief Test allocation with only CPUs
 */
TEST_F(ResourceMonitorTest, AllocateOnlyCPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    auto result = monitor_->allocate(4, 0, {}, {});
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->cpus.size(), 4);
    EXPECT_TRUE(result->gpus.empty());
}

/**
 * @brief Test allocation with only GPUs
 */
TEST_F(ResourceMonitorTest, AllocateOnlyGPUs) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    auto result = monitor_->allocate(0, 2, {}, {});
    
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->cpus.empty());
    EXPECT_EQ(result->gpus.size(), 2);
}

/**
 * @brief Test allocation fails and rolls back GPU allocation
 */
TEST_F(ResourceMonitorTest, AllocationRollbackOnCPUFailure) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(80.0);  // All CPUs busy
    
    // Try to allocate GPUs and CPUs - should fail on CPUs
    auto result = monitor_->allocate(4, 2, {}, {});
    
    EXPECT_FALSE(result.has_value());
    
    // GPUs should not be allocated (rolled back)
    auto allocated_gpus = monitor_->getAllocatedGPUs();
    EXPECT_TRUE(allocated_gpus.empty());
}

/**
 * @brief Test release of non-allocated resources (should be no-op)
 */
TEST_F(ResourceMonitorTest, ReleaseNonAllocatedResources) {
    auto mock_gpus = createMockGPUData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    setUniformCPUUtilization(10.0);
    
    // Release resources that were never allocated
    monitor_->release({0, 1, 2}, {0, 1});
    
    // Should not crash and allocation should still work
    auto result = monitor_->allocate(2, 2, {}, {});
    ASSERT_TRUE(result.has_value());
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(ResourceMonitorTest, ModifyGPUThreshold) {
    monitor_->setGPUMemoryThreshold(1000);
    EXPECT_EQ(monitor_->getGPUMemoryThreshold(), 1000);
}

TEST_F(ResourceMonitorTest, ModifyCPUThreshold) {
    monitor_->setCPUUtilThreshold(50.0);
    EXPECT_DOUBLE_EQ(monitor_->getCPUUtilThreshold(), 50.0);
}

// ============================================================================
// Status Query Tests
// ============================================================================

TEST_F(ResourceMonitorTest, GetGPUStatus) {
    auto mock_gpus = createMockGPUData({100, 2500, 100, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    
    auto status = monitor_->getGPUStatus();
    
    ASSERT_EQ(status.size(), 8);
    EXPECT_FALSE(status[0].is_busy);
    EXPECT_TRUE(status[1].is_busy);  // 2500 > 2000
}

TEST_F(ResourceMonitorTest, GetCPUStatus) {
    std::map<int, double> utils;
    for (int i = 0; i < 64; ++i) {
        utils[i] = static_cast<double>(i);
    }
    monitor_->setMockCPUUtilization(utils);
    
    auto status = monitor_->getCPUStatus();
    
    ASSERT_EQ(status.size(), 64);
    EXPECT_DOUBLE_EQ(status[0].utilization, 0.0);
    EXPECT_DOUBLE_EQ(status[10].utilization, 10.0);
}

TEST_F(ResourceMonitorTest, GetAvailableGPUs) {
    auto mock_gpus = createMockGPUData({3000, 100, 3000, 100, 100, 100, 100, 100});
    monitor_->setMockGPUData(mock_gpus);
    
    auto available = monitor_->getAvailableGPUs();
    
    // GPUs 0 and 2 are busy
    ASSERT_EQ(available.size(), 6);
    EXPECT_EQ(available[0], 1);
    EXPECT_EQ(available[1], 3);
}

TEST_F(ResourceMonitorTest, GetAvailableCPUs) {
    setUniformCPUUtilization(10.0);
    
    // Allocate some CPUs
    monitor_->cpuMonitor().allocateCPUs({0, 1, 32, 33});
    
    auto available_group1 = monitor_->getAvailableCPUs(1);
    auto available_group2 = monitor_->getAvailableCPUs(2);
    
    EXPECT_EQ(available_group1.size(), 30);  // 32 - 2 allocated
    EXPECT_EQ(available_group2.size(), 30);  // 32 - 2 allocated
}

} // namespace testing
} // namespace myqueue

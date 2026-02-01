/**
 * @file test_gpu_monitor.cpp
 * @brief Unit tests for GPU monitoring functionality
 * 
 * Tests GPU status detection, threshold logic, and allocation tracking.
 * Uses mock mode to avoid dependency on actual nvidia-smi.
 */

#include "myqueue/gpu_monitor.h"
#include "myqueue/config.h"

#include <gtest/gtest.h>
#include <vector>
#include <set>

namespace myqueue {
namespace testing {

/**
 * @brief Test fixture for GPUMonitor tests
 */
class GPUMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create monitor with default threshold (2000 MB) and 8 GPUs
        monitor_ = std::make_unique<GPUMonitor>(2000, 8);
        monitor_->setMockMode(true);
    }
    
    /**
     * @brief Create mock GPU data with specified memory usage
     * @param memory_values Vector of memory usage values for each GPU
     * @return Vector of GPUInfo with the specified memory usage
     */
    std::vector<GPUInfo> createMockData(const std::vector<size_t>& memory_values) {
        std::vector<GPUInfo> gpus;
        for (size_t i = 0; i < memory_values.size(); ++i) {
            GPUInfo info;
            info.device_id = static_cast<int>(i);
            info.memory_used_mb = memory_values[i];
            info.memory_total_mb = 16384;  // 16 GB
            info.is_busy = false;
            info.is_allocated = false;
            gpus.push_back(info);
        }
        return gpus;
    }
    
    std::unique_ptr<GPUMonitor> monitor_;
};

// ============================================================================
// Basic Construction Tests
// ============================================================================

TEST_F(GPUMonitorTest, ConstructWithConfig) {
    Config config;
    config.gpu_memory_threshold_mb = 3000;
    config.total_gpus = 4;
    
    GPUMonitor monitor(config);
    EXPECT_EQ(monitor.getMemoryThreshold(), 3000);
}

TEST_F(GPUMonitorTest, ConstructWithThreshold) {
    GPUMonitor monitor(1500, 4);
    EXPECT_EQ(monitor.getMemoryThreshold(), 1500);
}

TEST_F(GPUMonitorTest, DefaultThreshold) {
    GPUMonitor monitor;
    EXPECT_EQ(monitor.getMemoryThreshold(), 2000);
}

// ============================================================================
// GPU Busy Threshold Tests
// ============================================================================

/**
 * @brief Test that GPU is marked as busy when memory exceeds threshold
 * 
 * Validates: Requirements 5.2, 5.3
 * WHEN GPU memory usage exceeds the configured threshold (default 2000MB),
 * THE Resource_Monitor SHALL mark the GPU as busy
 */
TEST_F(GPUMonitorTest, GPUBusyWhenMemoryExceedsThreshold) {
    // Set up mock data with GPU 0 using 2500 MB (above 2000 MB threshold)
    auto mock_data = createMockData({2500, 1000, 500, 100, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    // Query GPUs
    auto gpus = monitor_->queryGPUs();
    
    // GPU 0 should be busy (2500 > 2000)
    ASSERT_GE(gpus.size(), 1);
    EXPECT_TRUE(gpus[0].is_busy);
    EXPECT_TRUE(monitor_->isGPUBusy(0));
}

/**
 * @brief Test that GPU is available when memory is below threshold
 * 
 * Validates: Requirements 5.3
 * WHEN GPU memory usage is below the threshold,
 * THE Resource_Monitor SHALL mark the GPU as available
 */
TEST_F(GPUMonitorTest, GPUAvailableWhenMemoryBelowThreshold) {
    // Set up mock data with all GPUs using less than 2000 MB
    auto mock_data = createMockData({1999, 1000, 500, 100, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    // Query GPUs
    auto gpus = monitor_->queryGPUs();
    
    // GPU 0 should not be busy (1999 <= 2000)
    ASSERT_GE(gpus.size(), 1);
    EXPECT_FALSE(gpus[0].is_busy);
    EXPECT_FALSE(monitor_->isGPUBusy(0));
}

/**
 * @brief Test threshold boundary condition (exactly at threshold)
 * 
 * Validates: Requirements 5.2, 5.3
 * Memory usage exactly at threshold should NOT be considered busy
 * (busy is when usage EXCEEDS threshold)
 */
TEST_F(GPUMonitorTest, GPUNotBusyAtExactThreshold) {
    // Set up mock data with GPU 0 using exactly 2000 MB
    auto mock_data = createMockData({2000, 0, 0, 0, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    // Query GPUs
    auto gpus = monitor_->queryGPUs();
    
    // GPU 0 should NOT be busy (2000 is not > 2000)
    ASSERT_GE(gpus.size(), 1);
    EXPECT_FALSE(gpus[0].is_busy);
}

/**
 * @brief Test threshold boundary condition (just above threshold)
 */
TEST_F(GPUMonitorTest, GPUBusyJustAboveThreshold) {
    // Set up mock data with GPU 0 using 2001 MB
    auto mock_data = createMockData({2001, 0, 0, 0, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    // Query GPUs
    auto gpus = monitor_->queryGPUs();
    
    // GPU 0 should be busy (2001 > 2000)
    ASSERT_GE(gpus.size(), 1);
    EXPECT_TRUE(gpus[0].is_busy);
}

/**
 * @brief Test custom threshold
 */
TEST_F(GPUMonitorTest, CustomThreshold) {
    monitor_->setMemoryThreshold(1000);
    
    // Set up mock data
    auto mock_data = createMockData({1500, 500, 0, 0, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    // Query GPUs
    auto gpus = monitor_->queryGPUs();
    
    // GPU 0 should be busy (1500 > 1000)
    ASSERT_GE(gpus.size(), 2);
    EXPECT_TRUE(gpus[0].is_busy);
    // GPU 1 should not be busy (500 <= 1000)
    EXPECT_FALSE(gpus[1].is_busy);
}

// ============================================================================
// GPU Allocation Tests
// ============================================================================

/**
 * @brief Test that allocated GPUs are marked as busy
 */
TEST_F(GPUMonitorTest, AllocatedGPUsAreBusy) {
    // Set up mock data with all GPUs having low memory usage
    auto mock_data = createMockData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    // Allocate GPU 0 and 1
    monitor_->allocateGPUs({0, 1});
    
    // GPU 0 and 1 should be busy due to allocation
    EXPECT_TRUE(monitor_->isGPUBusy(0));
    EXPECT_TRUE(monitor_->isGPUBusy(1));
    
    // GPU 2 should not be busy
    EXPECT_FALSE(monitor_->isGPUBusy(2));
}

/**
 * @brief Test GPU release
 */
TEST_F(GPUMonitorTest, ReleasedGPUsAreAvailable) {
    // Set up mock data with all GPUs having low memory usage
    auto mock_data = createMockData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    // Allocate and then release GPU 0
    monitor_->allocateGPUs({0});
    EXPECT_TRUE(monitor_->isGPUBusy(0));
    
    monitor_->releaseGPUs({0});
    EXPECT_FALSE(monitor_->isGPUBusy(0));
}

/**
 * @brief Test getAllocatedGPUs
 */
TEST_F(GPUMonitorTest, GetAllocatedGPUs) {
    monitor_->allocateGPUs({0, 2, 4});
    
    auto allocated = monitor_->getAllocatedGPUs();
    EXPECT_EQ(allocated.size(), 3);
    EXPECT_TRUE(allocated.count(0) > 0);
    EXPECT_TRUE(allocated.count(2) > 0);
    EXPECT_TRUE(allocated.count(4) > 0);
    EXPECT_TRUE(allocated.count(1) == 0);
}

// ============================================================================
// Available GPUs Tests
// ============================================================================

/**
 * @brief Test getAvailableGPUs returns GPUs in order
 * 
 * Validates: Requirements 5.9
 * WHEN allocating resources, THE Scheduler SHALL check GPUs in order (0,1,2,3,4,5,6,7)
 */
TEST_F(GPUMonitorTest, AvailableGPUsInOrder) {
    // Set up mock data with some GPUs busy
    // GPU 0: busy (high memory), GPU 1: available, GPU 2: available, etc.
    auto mock_data = createMockData({3000, 100, 100, 3000, 100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    auto available = monitor_->getAvailableGPUs();
    
    // Should return GPUs in order: 1, 2, 4, 5, 6, 7 (0 and 3 are busy)
    ASSERT_EQ(available.size(), 6);
    EXPECT_EQ(available[0], 1);
    EXPECT_EQ(available[1], 2);
    EXPECT_EQ(available[2], 4);
    EXPECT_EQ(available[3], 5);
    EXPECT_EQ(available[4], 6);
    EXPECT_EQ(available[5], 7);
}

/**
 * @brief Test getAvailableGPUs excludes allocated GPUs
 */
TEST_F(GPUMonitorTest, AvailableGPUsExcludesAllocated) {
    // Set up mock data with all GPUs having low memory
    auto mock_data = createMockData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    // Allocate GPU 0 and 2
    monitor_->allocateGPUs({0, 2});
    
    auto available = monitor_->getAvailableGPUs();
    
    // Should return GPUs: 1, 3, 4, 5, 6, 7 (0 and 2 are allocated)
    ASSERT_EQ(available.size(), 6);
    EXPECT_EQ(available[0], 1);
    EXPECT_EQ(available[1], 3);
}

/**
 * @brief Test getAvailableGPUs with no available GPUs
 */
TEST_F(GPUMonitorTest, NoAvailableGPUs) {
    // Set up mock data with all GPUs busy
    auto mock_data = createMockData({3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000});
    monitor_->setMockData(mock_data);
    
    auto available = monitor_->getAvailableGPUs();
    
    EXPECT_TRUE(available.empty());
}

/**
 * @brief Test getAvailableGPUs with all GPUs available
 */
TEST_F(GPUMonitorTest, AllGPUsAvailable) {
    // Set up mock data with all GPUs having low memory
    auto mock_data = createMockData({100, 100, 100, 100, 100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    auto available = monitor_->getAvailableGPUs();
    
    // All 8 GPUs should be available
    ASSERT_EQ(available.size(), 8);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(available[i], i);
    }
}

// ============================================================================
// nvidia-smi Output Parsing Tests
// ============================================================================

/**
 * @brief Test parsing of nvidia-smi output format
 * 
 * This tests the internal parsing logic by using mock mode
 * and verifying the GPUInfo structure is correctly populated.
 */
TEST_F(GPUMonitorTest, GPUInfoFieldsPopulated) {
    std::vector<GPUInfo> mock_data;
    
    GPUInfo gpu0;
    gpu0.device_id = 0;
    gpu0.memory_used_mb = 1234;
    gpu0.memory_total_mb = 16384;
    mock_data.push_back(gpu0);
    
    GPUInfo gpu1;
    gpu1.device_id = 1;
    gpu1.memory_used_mb = 5678;
    gpu1.memory_total_mb = 16384;
    mock_data.push_back(gpu1);
    
    monitor_->setMockData(mock_data);
    
    auto gpus = monitor_->queryGPUs();
    
    ASSERT_EQ(gpus.size(), 2);
    
    EXPECT_EQ(gpus[0].device_id, 0);
    EXPECT_EQ(gpus[0].memory_used_mb, 1234);
    EXPECT_EQ(gpus[0].memory_total_mb, 16384);
    EXPECT_FALSE(gpus[0].is_busy);  // 1234 <= 2000
    
    EXPECT_EQ(gpus[1].device_id, 1);
    EXPECT_EQ(gpus[1].memory_used_mb, 5678);
    EXPECT_EQ(gpus[1].memory_total_mb, 16384);
    EXPECT_TRUE(gpus[1].is_busy);  // 5678 > 2000
}

// ============================================================================
// GPUInfo Equality Tests
// ============================================================================

TEST_F(GPUMonitorTest, GPUInfoEquality) {
    GPUInfo a, b;
    a.device_id = 0;
    a.memory_used_mb = 1000;
    a.memory_total_mb = 16384;
    a.is_busy = false;
    a.is_allocated = false;
    
    b = a;
    EXPECT_EQ(a, b);
    
    b.memory_used_mb = 2000;
    EXPECT_NE(a, b);
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @brief Test with zero memory usage
 */
TEST_F(GPUMonitorTest, ZeroMemoryUsage) {
    auto mock_data = createMockData({0, 0, 0, 0, 0, 0, 0, 0});
    monitor_->setMockData(mock_data);
    
    auto gpus = monitor_->queryGPUs();
    
    for (const auto& gpu : gpus) {
        EXPECT_FALSE(gpu.is_busy);
    }
}

/**
 * @brief Test with very high memory usage
 */
TEST_F(GPUMonitorTest, VeryHighMemoryUsage) {
    auto mock_data = createMockData({100000, 100000, 100000, 100000, 
                                      100000, 100000, 100000, 100000});
    monitor_->setMockData(mock_data);
    
    auto gpus = monitor_->queryGPUs();
    
    for (const auto& gpu : gpus) {
        EXPECT_TRUE(gpu.is_busy);
    }
}

/**
 * @brief Test with fewer GPUs than expected
 */
TEST_F(GPUMonitorTest, FewerGPUsThanExpected) {
    // Only 4 GPUs in mock data, but monitor expects 8
    auto mock_data = createMockData({100, 100, 100, 100});
    monitor_->setMockData(mock_data);
    
    auto gpus = monitor_->queryGPUs();
    
    // Should return only 4 GPUs
    EXPECT_EQ(gpus.size(), 4);
}

// ============================================================================
// Static Method Tests
// ============================================================================

/**
 * @brief Test isNvidiaSmiAvailable (may pass or fail depending on system)
 * 
 * This test documents the expected behavior but doesn't assert
 * since the result depends on the test environment.
 */
TEST_F(GPUMonitorTest, NvidiaSmiAvailabilityCheck) {
    // Just verify the method doesn't crash
    bool available = GPUMonitor::isNvidiaSmiAvailable();
    // Result depends on whether nvidia-smi is installed
    (void)available;  // Suppress unused variable warning
}

} // namespace testing
} // namespace myqueue

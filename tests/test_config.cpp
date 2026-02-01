/**
 * @file test_config.cpp
 * @brief Unit tests for Config class
 * 
 * Tests command-line argument parsing, JSON serialization/deserialization,
 * and path initialization.
 */

#include "myqueue/config.h"
#include "myqueue/errors.h"

#include <gtest/gtest.h>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

namespace myqueue {
namespace {

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original environment
        const char* home = std::getenv("HOME");
        const char* user = std::getenv("USER");
        original_home_ = home ? home : "";
        original_user_ = user ? user : "";
    }
    
    void TearDown() override {
        // Restore original environment
        if (!original_home_.empty()) {
            setenv("HOME", original_home_.c_str(), 1);
        }
        if (!original_user_.empty()) {
            setenv("USER", original_user_.c_str(), 1);
        }
    }
    
    std::string original_home_;
    std::string original_user_;
};

// ========== Default Values Tests ==========

TEST_F(ConfigTest, DefaultValues) {
    Config config;
    
    // Resource thresholds
    EXPECT_EQ(config.gpu_memory_threshold_mb, 2000u);
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 40.0);
    EXPECT_EQ(config.cpu_check_duration_ms, 3000);
    
    // Scheduling parameters
    EXPECT_EQ(config.scheduling_interval_ms, 1000);
    EXPECT_EQ(config.process_check_interval_ms, 500);
    
    // System topology
    EXPECT_EQ(config.total_cpus, 64);
    EXPECT_EQ(config.total_gpus, 8);
    
    // Logging
    EXPECT_FALSE(config.enable_logging);
    EXPECT_TRUE(config.log_dir.empty());
}

// ========== Command-line Argument Parsing Tests ==========

TEST_F(ConfigTest, FromArgsNoArguments) {
    char* argv[] = {const_cast<char*>("myqueue")};
    Config config = Config::fromArgs(1, argv);
    
    // Should have default values
    EXPECT_EQ(config.gpu_memory_threshold_mb, 2000u);
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 40.0);
    EXPECT_FALSE(config.enable_logging);
    
    // Should have auto-detected paths
    EXPECT_FALSE(config.socket_path.empty());
    EXPECT_FALSE(config.data_dir.empty());
    EXPECT_TRUE(config.socket_path.find("/tmp/myqueue_") == 0);
    EXPECT_TRUE(config.socket_path.find(".sock") != std::string::npos);
}

TEST_F(ConfigTest, FromArgsWithLog) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--log"),
        const_cast<char*>("/var/log/myqueue")
    };
    Config config = Config::fromArgs(3, argv);
    
    EXPECT_TRUE(config.enable_logging);
    EXPECT_EQ(config.log_dir, "/var/log/myqueue");
}

TEST_F(ConfigTest, FromArgsWithGpuMemory) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--gpumemory"),
        const_cast<char*>("4000")
    };
    Config config = Config::fromArgs(3, argv);
    
    EXPECT_EQ(config.gpu_memory_threshold_mb, 4000u);
}

TEST_F(ConfigTest, FromArgsWithCpuUsage) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--cpuusage"),
        const_cast<char*>("50.5")
    };
    Config config = Config::fromArgs(3, argv);
    
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 50.5);
}

TEST_F(ConfigTest, FromArgsWithAllOptions) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--log"),
        const_cast<char*>("/tmp/logs"),
        const_cast<char*>("--gpumemory"),
        const_cast<char*>("3000"),
        const_cast<char*>("--cpuusage"),
        const_cast<char*>("30")
    };
    Config config = Config::fromArgs(7, argv);
    
    EXPECT_TRUE(config.enable_logging);
    EXPECT_EQ(config.log_dir, "/tmp/logs");
    EXPECT_EQ(config.gpu_memory_threshold_mb, 3000u);
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 30.0);
}

TEST_F(ConfigTest, FromArgsInvalidGpuMemory) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--gpumemory"),
        const_cast<char*>("invalid")
    };
    Config config = Config::fromArgs(3, argv);
    
    // Should keep default value on parse error
    EXPECT_EQ(config.gpu_memory_threshold_mb, 2000u);
}

TEST_F(ConfigTest, FromArgsInvalidCpuUsage) {
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--cpuusage"),
        const_cast<char*>("not_a_number")
    };
    Config config = Config::fromArgs(3, argv);
    
    // Should keep default value on parse error
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 40.0);
}

TEST_F(ConfigTest, FromArgsMissingValue) {
    // --log without value
    char* argv[] = {
        const_cast<char*>("myqueue"),
        const_cast<char*>("--log")
    };
    Config config = Config::fromArgs(2, argv);
    
    // Should not crash, logging should remain disabled
    EXPECT_FALSE(config.enable_logging);
}

// ========== Path Auto-detection Tests ==========

TEST_F(ConfigTest, SocketPathFormat) {
    char* argv[] = {const_cast<char*>("myqueue")};
    Config config = Config::fromArgs(1, argv);
    
    // Socket path should be /tmp/myqueue_<username>.sock
    EXPECT_TRUE(config.socket_path.find("/tmp/myqueue_") == 0);
    EXPECT_TRUE(config.socket_path.find(".sock") != std::string::npos);
}

TEST_F(ConfigTest, DataDirFormat) {
    char* argv[] = {const_cast<char*>("myqueue")};
    Config config = Config::fromArgs(1, argv);
    
    // Data dir should be ~/.myqueue/<hostname>/
    EXPECT_TRUE(config.data_dir.find("/.myqueue/") != std::string::npos);
}

// ========== JSON Serialization Tests ==========

TEST_F(ConfigTest, ToJsonContainsAllFields) {
    Config config;
    config.gpu_memory_threshold_mb = 3000;
    config.cpu_util_threshold = 50.0;
    config.cpu_check_duration_ms = 5000;
    config.scheduling_interval_ms = 2000;
    config.process_check_interval_ms = 1000;
    config.total_cpus = 32;
    config.total_gpus = 4;
    config.socket_path = "/tmp/test.sock";
    config.data_dir = "/home/test/.myqueue/host1";
    config.log_dir = "/var/log/myqueue";
    config.enable_logging = true;
    
    std::string json = config.toJson();
    
    // Verify JSON contains all fields
    EXPECT_TRUE(json.find("\"gpu_memory_threshold_mb\": 3000") != std::string::npos);
    EXPECT_TRUE(json.find("\"cpu_util_threshold\": 50.0") != std::string::npos);
    EXPECT_TRUE(json.find("\"cpu_check_duration_ms\": 5000") != std::string::npos);
    EXPECT_TRUE(json.find("\"scheduling_interval_ms\": 2000") != std::string::npos);
    EXPECT_TRUE(json.find("\"process_check_interval_ms\": 1000") != std::string::npos);
    EXPECT_TRUE(json.find("\"total_cpus\": 32") != std::string::npos);
    EXPECT_TRUE(json.find("\"total_gpus\": 4") != std::string::npos);
    EXPECT_TRUE(json.find("\"socket_path\": \"/tmp/test.sock\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"data_dir\": \"/home/test/.myqueue/host1\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"log_dir\": \"/var/log/myqueue\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"enable_logging\": true") != std::string::npos);
}

TEST_F(ConfigTest, FromJsonParsesAllFields) {
    std::string json = R"({
        "gpu_memory_threshold_mb": 4000,
        "cpu_util_threshold": 60.5,
        "cpu_check_duration_ms": 4000,
        "scheduling_interval_ms": 1500,
        "process_check_interval_ms": 750,
        "total_cpus": 128,
        "total_gpus": 16,
        "socket_path": "/tmp/custom.sock",
        "data_dir": "/data/myqueue",
        "log_dir": "/logs",
        "enable_logging": true
    })";
    
    Config config = Config::fromJson(json);
    
    EXPECT_EQ(config.gpu_memory_threshold_mb, 4000u);
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 60.5);
    EXPECT_EQ(config.cpu_check_duration_ms, 4000);
    EXPECT_EQ(config.scheduling_interval_ms, 1500);
    EXPECT_EQ(config.process_check_interval_ms, 750);
    EXPECT_EQ(config.total_cpus, 128);
    EXPECT_EQ(config.total_gpus, 16);
    EXPECT_EQ(config.socket_path, "/tmp/custom.sock");
    EXPECT_EQ(config.data_dir, "/data/myqueue");
    EXPECT_EQ(config.log_dir, "/logs");
    EXPECT_TRUE(config.enable_logging);
}

TEST_F(ConfigTest, FromJsonPartialFields) {
    // JSON with only some fields - others should use defaults
    std::string json = R"({
        "gpu_memory_threshold_mb": 5000,
        "enable_logging": true
    })";
    
    Config config = Config::fromJson(json);
    
    EXPECT_EQ(config.gpu_memory_threshold_mb, 5000u);
    EXPECT_TRUE(config.enable_logging);
    
    // Other fields should have defaults
    EXPECT_DOUBLE_EQ(config.cpu_util_threshold, 40.0);
    EXPECT_EQ(config.total_cpus, 64);
}

TEST_F(ConfigTest, FromJsonInvalidJson) {
    std::string invalid_json = "{ invalid json }";
    
    EXPECT_THROW(Config::fromJson(invalid_json), MyQueueException);
}

TEST_F(ConfigTest, JsonRoundTrip) {
    Config original;
    original.gpu_memory_threshold_mb = 2500;
    original.cpu_util_threshold = 35.5;
    original.cpu_check_duration_ms = 4000;
    original.scheduling_interval_ms = 1500;
    original.process_check_interval_ms = 750;
    original.total_cpus = 48;
    original.total_gpus = 6;
    original.socket_path = "/tmp/roundtrip.sock";
    original.data_dir = "/home/user/.myqueue/testhost";
    original.log_dir = "/var/log/test";
    original.enable_logging = true;
    
    std::string json = original.toJson();
    Config restored = Config::fromJson(json);
    
    EXPECT_EQ(original, restored);
}

// ========== Equality Operator Tests ==========

TEST_F(ConfigTest, EqualityOperator) {
    Config a, b;
    EXPECT_EQ(a, b);
    
    a.gpu_memory_threshold_mb = 3000;
    EXPECT_NE(a, b);
    
    b.gpu_memory_threshold_mb = 3000;
    EXPECT_EQ(a, b);
}

TEST_F(ConfigTest, EqualityAllFields) {
    Config a, b;
    
    // Modify all fields in a
    a.gpu_memory_threshold_mb = 1000;
    a.cpu_util_threshold = 20.0;
    a.cpu_check_duration_ms = 2000;
    a.scheduling_interval_ms = 500;
    a.process_check_interval_ms = 250;
    a.total_cpus = 32;
    a.total_gpus = 4;
    a.socket_path = "/tmp/a.sock";
    a.data_dir = "/data/a";
    a.log_dir = "/log/a";
    a.enable_logging = true;
    
    // b still has defaults
    EXPECT_NE(a, b);
    
    // Copy all fields to b
    b = a;
    EXPECT_EQ(a, b);
}

} // namespace
} // namespace myqueue

/**
 * @file task.cpp
 * @brief Implementation of Task JSON serialization/deserialization
 * 
 * Uses nlohmann/json library for JSON handling.
 */

#include "myqueue/task.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <iomanip>
#include <sstream>

namespace myqueue {

namespace {

/**
 * @brief Convert time_point to ISO 8601 string
 * @param tp Time point to convert
 * @return ISO 8601 formatted string (e.g., "2024-01-15T10:30:00Z")
 */
std::string timePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
    gmtime_r(&time_t_val, &tm_val);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/**
 * @brief Parse ISO 8601 string to time_point
 * @param str ISO 8601 formatted string
 * @return Parsed time_point
 * @throws std::runtime_error if parsing fails
 */
std::chrono::system_clock::time_point stringToTimePoint(const std::string& str) {
    std::tm tm_val = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
    
    if (iss.fail()) {
        throw std::runtime_error("Failed to parse time string: " + str);
    }
    
    // Convert to time_t (UTC)
    time_t time_t_val = timegm(&tm_val);
    return std::chrono::system_clock::from_time_t(time_t_val);
}

} // anonymous namespace

std::string Task::toJson() const {
    nlohmann::json j;
    
    // Basic fields
    j["id"] = id;
    j["script_path"] = script_path;
    j["workdir"] = workdir;
    
    // Resource requirements
    j["ncpu"] = ncpu;
    j["ngpu"] = ngpu;
    j["specific_cpus"] = specific_cpus;
    j["specific_gpus"] = specific_gpus;
    j["log_file"] = log_file;
    
    // Allocated resources
    j["allocated_cpus"] = allocated_cpus;
    j["allocated_gpus"] = allocated_gpus;
    
    // Status and execution info
    j["status"] = taskStatusToString(status);
    j["pid"] = pid;
    j["exit_code"] = exit_code;
    
    // Timestamps
    j["submit_time"] = timePointToString(submit_time);
    
    if (start_time.has_value()) {
        j["start_time"] = timePointToString(start_time.value());
    } else {
        j["start_time"] = nullptr;
    }
    
    if (end_time.has_value()) {
        j["end_time"] = timePointToString(end_time.value());
    } else {
        j["end_time"] = nullptr;
    }
    
    return j.dump();
}

Task Task::fromJson(const std::string& json) {
    try {
        nlohmann::json j = nlohmann::json::parse(json);
        
        Task task;
        
        // Basic fields
        task.id = j.at("id").get<uint64_t>();
        task.script_path = j.at("script_path").get<std::string>();
        task.workdir = j.at("workdir").get<std::string>();
        
        // Resource requirements
        task.ncpu = j.at("ncpu").get<int>();
        task.ngpu = j.at("ngpu").get<int>();
        task.specific_cpus = j.at("specific_cpus").get<std::vector<int>>();
        task.specific_gpus = j.at("specific_gpus").get<std::vector<int>>();
        task.log_file = j.value("log_file", "");
        
        // Allocated resources
        task.allocated_cpus = j.at("allocated_cpus").get<std::vector<int>>();
        task.allocated_gpus = j.at("allocated_gpus").get<std::vector<int>>();
        
        // Status and execution info
        task.status = taskStatusFromString(j.at("status").get<std::string>());
        task.pid = j.at("pid").get<pid_t>();
        task.exit_code = j.at("exit_code").get<int>();
        
        // Timestamps
        task.submit_time = stringToTimePoint(j.at("submit_time").get<std::string>());
        
        if (!j.at("start_time").is_null()) {
            task.start_time = stringToTimePoint(j.at("start_time").get<std::string>());
        }
        
        if (!j.at("end_time").is_null()) {
            task.end_time = stringToTimePoint(j.at("end_time").get<std::string>());
        }
        
        return task;
        
    } catch (const nlohmann::json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR, 
                               std::string("JSON parse error: ") + e.what());
    }
}

bool Task::operator==(const Task& other) const {
    // Compare basic fields
    if (id != other.id) return false;
    if (script_path != other.script_path) return false;
    if (workdir != other.workdir) return false;
    
    // Compare resource requirements
    if (ncpu != other.ncpu) return false;
    if (ngpu != other.ngpu) return false;
    if (specific_cpus != other.specific_cpus) return false;
    if (specific_gpus != other.specific_gpus) return false;
    if (log_file != other.log_file) return false;
    
    // Compare allocated resources
    if (allocated_cpus != other.allocated_cpus) return false;
    if (allocated_gpus != other.allocated_gpus) return false;
    
    // Compare status and execution info
    if (status != other.status) return false;
    if (pid != other.pid) return false;
    if (exit_code != other.exit_code) return false;
    
    // Compare timestamps (with second precision due to serialization)
    auto toSeconds = [](const std::chrono::system_clock::time_point& tp) {
        return std::chrono::duration_cast<std::chrono::seconds>(
            tp.time_since_epoch()).count();
    };
    
    if (toSeconds(submit_time) != toSeconds(other.submit_time)) return false;
    
    // Compare optional timestamps
    if (start_time.has_value() != other.start_time.has_value()) return false;
    if (start_time.has_value() && 
        toSeconds(start_time.value()) != toSeconds(other.start_time.value())) {
        return false;
    }
    
    if (end_time.has_value() != other.end_time.has_value()) return false;
    if (end_time.has_value() && 
        toSeconds(end_time.value()) != toSeconds(other.end_time.value())) {
        return false;
    }
    
    return true;
}

} // namespace myqueue

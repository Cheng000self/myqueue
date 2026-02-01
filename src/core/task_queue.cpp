/**
 * @file task_queue.cpp
 * @brief Implementation of TaskQueue class
 * 
 * Provides thread-safe task management with persistence support.
 */

#include "myqueue/task_queue.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace myqueue {

TaskQueue::TaskQueue(const std::string& data_dir)
    : data_dir_(data_dir), next_id_(1) {
}

uint64_t TaskQueue::submit(const SubmitRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Task task;
    task.id = next_id_++;
    task.script_path = req.script_path;
    task.workdir = req.workdir;
    task.ncpu = req.ncpu;
    task.ngpu = req.ngpu;
    task.specific_cpus = req.specific_cpus;
    task.specific_gpus = req.specific_gpus;
    task.log_file = req.log_file;
    task.status = TaskStatus::PENDING;
    task.submit_time = std::chrono::system_clock::now();
    
    tasks_[task.id] = task;
    
    return task.id;
}

std::vector<uint64_t> TaskQueue::submitBatch(const std::string& script,
                                              const std::vector<std::string>& workdirs,
                                              int ncpu, int ngpu) {
    std::vector<uint64_t> ids;
    ids.reserve(workdirs.size());
    
    for (const auto& workdir : workdirs) {
        SubmitRequest req;
        req.script_path = script;
        req.workdir = workdir;
        req.ncpu = ncpu;
        req.ngpu = ngpu;
        
        ids.push_back(submit(req));
    }
    
    return ids;
}

std::pair<std::vector<uint64_t>, std::vector<std::string>>
TaskQueue::submitBatchFromFile(const std::string& script,
                                const std::string& workdirs_file,
                                int ncpu, int ngpu) {
    auto [valid_dirs, invalid_dirs] = parseWorkdirsFile(workdirs_file);
    
    auto ids = submitBatch(script, valid_dirs, ncpu, ngpu);
    
    return {ids, invalid_dirs};
}

std::pair<std::vector<std::string>, std::vector<std::string>>
TaskQueue::parseWorkdirsFile(const std::string& filepath) {
    std::vector<std::string> valid_dirs;
    std::vector<std::string> invalid_dirs;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return {valid_dirs, invalid_dirs};
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            // Empty line or whitespace only
            continue;
        }
        line = line.substr(start);
        
        // Trim trailing whitespace
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            line = line.substr(0, end + 1);
        }
        
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Skip comments (lines starting with #)
        if (line[0] == '#') {
            continue;
        }
        
        // Check if directory exists
        struct stat st;
        if (stat(line.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            valid_dirs.push_back(line);
        } else {
            invalid_dirs.push_back(line);
        }
    }
    
    return {valid_dirs, invalid_dirs};
}

std::optional<Task> TaskQueue::getTask(uint64_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Task> TaskQueue::getPendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> pending;
    for (const auto& [id, task] : tasks_) {
        if (task.status == TaskStatus::PENDING) {
            pending.push_back(task);
        }
    }
    
    // Sort by submission time (FIFO order)
    std::sort(pending.begin(), pending.end(),
              [](const Task& a, const Task& b) {
                  return a.submit_time < b.submit_time;
              });
    
    return pending;
}

std::vector<Task> TaskQueue::getRunningTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> running;
    for (const auto& [id, task] : tasks_) {
        if (task.status == TaskStatus::RUNNING) {
            running.push_back(task);
        }
    }
    
    return running;
}

std::vector<Task> TaskQueue::getAllTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Task> all;
    all.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        all.push_back(task);
    }
    
    return all;
}

bool TaskQueue::setTaskRunning(uint64_t id, pid_t pid,
                                const std::vector<int>& cpus,
                                const std::vector<int>& gpus) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    
    Task& task = it->second;
    
    // Can only transition from PENDING to RUNNING
    if (task.status != TaskStatus::PENDING) {
        return false;
    }
    
    task.status = TaskStatus::RUNNING;
    task.pid = pid;
    task.allocated_cpus = cpus;
    task.allocated_gpus = gpus;
    task.start_time = std::chrono::system_clock::now();
    
    return true;
}

bool TaskQueue::setTaskCompleted(uint64_t id, int exit_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    
    Task& task = it->second;
    
    // Can only transition from RUNNING to COMPLETED
    if (task.status != TaskStatus::RUNNING) {
        return false;
    }
    
    task.status = TaskStatus::COMPLETED;
    task.exit_code = exit_code;
    task.end_time = std::chrono::system_clock::now();
    
    return true;
}

bool TaskQueue::setTaskFailed(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    
    Task& task = it->second;
    
    // Can transition from PENDING or RUNNING to FAILED
    if (task.status != TaskStatus::PENDING && task.status != TaskStatus::RUNNING) {
        return false;
    }
    
    task.status = TaskStatus::FAILED;
    task.end_time = std::chrono::system_clock::now();
    
    return true;
}

bool TaskQueue::deleteTask(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    
    // Mark as cancelled if not already terminal
    if (!it->second.isTerminal()) {
        it->second.status = TaskStatus::CANCELLED;
        it->second.end_time = std::chrono::system_clock::now();
    }
    
    tasks_.erase(it);
    return true;
}

std::vector<bool> TaskQueue::deleteTasks(const std::vector<uint64_t>& ids) {
    std::vector<bool> results;
    results.reserve(ids.size());
    
    // Note: We don't hold the lock for the entire operation to avoid
    // long lock times. Each deleteTask call acquires its own lock.
    for (uint64_t id : ids) {
        results.push_back(deleteTask(id));
    }
    
    return results;
}

std::vector<uint64_t> TaskQueue::parseIdRange(const std::string& range_str) {
    std::vector<uint64_t> ids;
    
    // Check if it's a range (contains '-')
    size_t dash_pos = range_str.find('-');
    if (dash_pos != std::string::npos && dash_pos > 0 && dash_pos < range_str.length() - 1) {
        // It's a range like "1-10"
        try {
            uint64_t start = std::stoull(range_str.substr(0, dash_pos));
            uint64_t end = std::stoull(range_str.substr(dash_pos + 1));
            
            if (start <= end) {
                for (uint64_t i = start; i <= end; ++i) {
                    ids.push_back(i);
                }
            }
        } catch (const std::exception&) {
            // Invalid format, return empty
        }
    } else {
        // Single ID
        try {
            ids.push_back(std::stoull(range_str));
        } catch (const std::exception&) {
            // Invalid format, return empty
        }
    }
    
    return ids;
}

void TaskQueue::save() {
    if (data_dir_.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create data directory if it doesn't exist
    struct stat st;
    if (stat(data_dir_.c_str(), &st) != 0) {
        // Directory doesn't exist, create it recursively
        std::string cmd = "mkdir -p " + data_dir_;
        int ret = system(cmd.c_str());
        if (ret != 0) {
            // Continue anyway, will fail when trying to write file
        }
    }
    
    nlohmann::json j;
    j["next_id"] = next_id_;
    
    nlohmann::json tasks_array = nlohmann::json::array();
    for (const auto& [id, task] : tasks_) {
        tasks_array.push_back(nlohmann::json::parse(task.toJson()));
    }
    j["tasks"] = tasks_array;
    
    std::string filepath = getTasksFilePath();
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(2);
        file.close();
    }
}

void TaskQueue::load() {
    if (data_dir_.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string filepath = getTasksFilePath();
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // File doesn't exist, start with empty queue
        return;
    }
    
    try {
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        nlohmann::json j = nlohmann::json::parse(buffer.str());
        
        next_id_ = j.at("next_id").get<uint64_t>();
        
        tasks_.clear();
        for (const auto& task_json : j.at("tasks")) {
            Task task = Task::fromJson(task_json.dump());
            tasks_[task.id] = task;
        }
    } catch (const std::exception& e) {
        // If loading fails, start with empty queue
        tasks_.clear();
        next_id_ = 1;
    }
}

uint64_t TaskQueue::getNextId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_id_;
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty();
}

void TaskQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.clear();
    next_id_ = 1;
}

std::string TaskQueue::getTasksFilePath() const {
    if (data_dir_.empty()) {
        return "";
    }
    return data_dir_ + "/tasks.json";
}

} // namespace myqueue

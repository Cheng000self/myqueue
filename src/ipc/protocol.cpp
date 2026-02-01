/**
 * @file protocol.cpp
 * @brief Implementation of IPC protocol message serialization
 */

#include "myqueue/protocol.h"
#include "myqueue/errors.h"
#include "json.hpp"

#include <stdexcept>

namespace myqueue {

using json = nlohmann::json;

// ============================================================================
// MsgType conversion functions
// ============================================================================

std::string msgTypeToString(MsgType type) {
    switch (type) {
        case MsgType::SUBMIT:          return "SUBMIT";
        case MsgType::QUERY_QUEUE:     return "QUERY_QUEUE";
        case MsgType::DELETE_TASK:     return "DELETE_TASK";
        case MsgType::SHUTDOWN:        return "SHUTDOWN";
        case MsgType::DELETE_ALL:      return "DELETE_ALL";
        case MsgType::QUERY_QUEUE_ALL: return "QUERY_QUEUE_ALL";
        case MsgType::GET_TASK_INFO:   return "GET_TASK_INFO";
        case MsgType::GET_TASK_LOG:    return "GET_TASK_LOG";
        case MsgType::OK:              return "OK";
        case MsgType::ERROR:           return "ERROR";
        default:                       return "UNKNOWN";
    }
}

MsgType msgTypeFromString(const std::string& str) {
    if (str == "SUBMIT")          return MsgType::SUBMIT;
    if (str == "QUERY_QUEUE")     return MsgType::QUERY_QUEUE;
    if (str == "DELETE_TASK")     return MsgType::DELETE_TASK;
    if (str == "SHUTDOWN")        return MsgType::SHUTDOWN;
    if (str == "DELETE_ALL")      return MsgType::DELETE_ALL;
    if (str == "QUERY_QUEUE_ALL") return MsgType::QUERY_QUEUE_ALL;
    if (str == "GET_TASK_INFO")   return MsgType::GET_TASK_INFO;
    if (str == "GET_TASK_LOG")    return MsgType::GET_TASK_LOG;
    if (str == "OK")              return MsgType::OK;
    if (str == "ERROR")           return MsgType::ERROR;
    throw std::invalid_argument("Unknown message type: " + str);
}

// ============================================================================
// SubmitRequest
// ============================================================================

std::string SubmitRequest::toJson() const {
    json j;
    j["script_path"] = script_path;
    j["workdir"] = workdir;
    j["ncpu"] = ncpu;
    j["ngpu"] = ngpu;
    j["specific_cpus"] = specific_cpus;
    j["specific_gpus"] = specific_gpus;
    j["log_file"] = log_file;
    return j.dump();
}

SubmitRequest SubmitRequest::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        SubmitRequest req;
        
        // Required fields
        if (!j.contains("script_path") || !j.contains("workdir")) {
            throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                "SubmitRequest missing required fields");
        }
        
        req.script_path = j["script_path"].get<std::string>();
        req.workdir = j["workdir"].get<std::string>();
        
        // Optional fields with defaults
        req.ncpu = j.value("ncpu", 1);
        req.ngpu = j.value("ngpu", 1);
        
        if (j.contains("specific_cpus")) {
            req.specific_cpus = j["specific_cpus"].get<std::vector<int>>();
        }
        if (j.contains("specific_gpus")) {
            req.specific_gpus = j["specific_gpus"].get<std::vector<int>>();
        }
        req.log_file = j.value("log_file", "");
        
        return req;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse SubmitRequest JSON: ") + e.what());
    }
}

bool SubmitRequest::operator==(const SubmitRequest& other) const {
    return script_path == other.script_path &&
           workdir == other.workdir &&
           ncpu == other.ncpu &&
           ngpu == other.ngpu &&
           specific_cpus == other.specific_cpus &&
           specific_gpus == other.specific_gpus &&
           log_file == other.log_file;
}

// ============================================================================
// DeleteRequest
// ============================================================================

std::string DeleteRequest::toJson() const {
    json j;
    j["task_ids"] = task_ids;
    return j.dump();
}

DeleteRequest DeleteRequest::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        DeleteRequest req;
        
        if (!j.contains("task_ids")) {
            throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                "DeleteRequest missing required field: task_ids");
        }
        
        req.task_ids = j["task_ids"].get<std::vector<uint64_t>>();
        
        return req;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse DeleteRequest JSON: ") + e.what());
    }
}

bool DeleteRequest::operator==(const DeleteRequest& other) const {
    return task_ids == other.task_ids;
}

// ============================================================================
// TaskInfo
// ============================================================================

std::string TaskInfo::toJson() const {
    json j;
    j["id"] = id;
    j["status"] = status;
    j["script"] = script;
    j["workdir"] = workdir;
    j["cpus"] = cpus;
    j["gpus"] = gpus;
    j["exit_code"] = exit_code;
    j["duration_seconds"] = duration_seconds;
    return j.dump();
}

TaskInfo TaskInfo::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        TaskInfo info;
        
        // Required fields
        if (!j.contains("id") || !j.contains("status") ||
            !j.contains("script") || !j.contains("workdir")) {
            throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                "TaskInfo missing required fields");
        }
        
        info.id = j["id"].get<uint64_t>();
        info.status = j["status"].get<std::string>();
        info.script = j["script"].get<std::string>();
        info.workdir = j["workdir"].get<std::string>();
        
        // Optional fields
        if (j.contains("cpus")) {
            info.cpus = j["cpus"].get<std::vector<int>>();
        }
        if (j.contains("gpus")) {
            info.gpus = j["gpus"].get<std::vector<int>>();
        }
        info.exit_code = j.value("exit_code", 0);
        info.duration_seconds = j.value("duration_seconds", 0);
        
        return info;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse TaskInfo JSON: ") + e.what());
    }
}

bool TaskInfo::operator==(const TaskInfo& other) const {
    return id == other.id &&
           status == other.status &&
           script == other.script &&
           workdir == other.workdir &&
           cpus == other.cpus &&
           gpus == other.gpus &&
           exit_code == other.exit_code &&
           duration_seconds == other.duration_seconds;
}

// ============================================================================
// QueueResponse
// ============================================================================

std::string QueueResponse::toJson() const {
    json j;
    
    // Serialize running tasks
    json running_arr = json::array();
    for (const auto& task : running) {
        json task_j;
        task_j["id"] = task.id;
        task_j["status"] = task.status;
        task_j["script"] = task.script;
        task_j["workdir"] = task.workdir;
        task_j["cpus"] = task.cpus;
        task_j["gpus"] = task.gpus;
        task_j["exit_code"] = task.exit_code;
        task_j["duration_seconds"] = task.duration_seconds;
        running_arr.push_back(task_j);
    }
    j["running"] = running_arr;
    
    // Serialize pending tasks
    json pending_arr = json::array();
    for (const auto& task : pending) {
        json task_j;
        task_j["id"] = task.id;
        task_j["status"] = task.status;
        task_j["script"] = task.script;
        task_j["workdir"] = task.workdir;
        task_j["cpus"] = task.cpus;
        task_j["gpus"] = task.gpus;
        task_j["exit_code"] = task.exit_code;
        task_j["duration_seconds"] = task.duration_seconds;
        pending_arr.push_back(task_j);
    }
    j["pending"] = pending_arr;
    
    // Serialize completed tasks
    json completed_arr = json::array();
    for (const auto& task : completed) {
        json task_j;
        task_j["id"] = task.id;
        task_j["status"] = task.status;
        task_j["script"] = task.script;
        task_j["workdir"] = task.workdir;
        task_j["cpus"] = task.cpus;
        task_j["gpus"] = task.gpus;
        task_j["exit_code"] = task.exit_code;
        task_j["duration_seconds"] = task.duration_seconds;
        completed_arr.push_back(task_j);
    }
    j["completed"] = completed_arr;
    
    return j.dump();
}

QueueResponse QueueResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        QueueResponse resp;
        
        // Parse running tasks
        if (j.contains("running")) {
            for (const auto& task_j : j["running"]) {
                TaskInfo info;
                info.id = task_j["id"].get<uint64_t>();
                info.status = task_j["status"].get<std::string>();
                info.script = task_j["script"].get<std::string>();
                info.workdir = task_j["workdir"].get<std::string>();
                if (task_j.contains("cpus")) {
                    info.cpus = task_j["cpus"].get<std::vector<int>>();
                }
                if (task_j.contains("gpus")) {
                    info.gpus = task_j["gpus"].get<std::vector<int>>();
                }
                info.exit_code = task_j.value("exit_code", 0);
                info.duration_seconds = task_j.value("duration_seconds", 0);
                resp.running.push_back(info);
            }
        }
        
        // Parse pending tasks
        if (j.contains("pending")) {
            for (const auto& task_j : j["pending"]) {
                TaskInfo info;
                info.id = task_j["id"].get<uint64_t>();
                info.status = task_j["status"].get<std::string>();
                info.script = task_j["script"].get<std::string>();
                info.workdir = task_j["workdir"].get<std::string>();
                if (task_j.contains("cpus")) {
                    info.cpus = task_j["cpus"].get<std::vector<int>>();
                }
                if (task_j.contains("gpus")) {
                    info.gpus = task_j["gpus"].get<std::vector<int>>();
                }
                info.exit_code = task_j.value("exit_code", 0);
                info.duration_seconds = task_j.value("duration_seconds", 0);
                resp.pending.push_back(info);
            }
        }
        
        // Parse completed tasks
        if (j.contains("completed")) {
            for (const auto& task_j : j["completed"]) {
                TaskInfo info;
                info.id = task_j["id"].get<uint64_t>();
                info.status = task_j["status"].get<std::string>();
                info.script = task_j["script"].get<std::string>();
                info.workdir = task_j["workdir"].get<std::string>();
                if (task_j.contains("cpus")) {
                    info.cpus = task_j["cpus"].get<std::vector<int>>();
                }
                if (task_j.contains("gpus")) {
                    info.gpus = task_j["gpus"].get<std::vector<int>>();
                }
                info.exit_code = task_j.value("exit_code", 0);
                info.duration_seconds = task_j.value("duration_seconds", 0);
                resp.completed.push_back(info);
            }
        }
        
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse QueueResponse JSON: ") + e.what());
    }
}

bool QueueResponse::operator==(const QueueResponse& other) const {
    return running == other.running && pending == other.pending && completed == other.completed;
}

// ============================================================================
// SubmitResponse
// ============================================================================

std::string SubmitResponse::toJson() const {
    json j;
    j["task_id"] = task_id;
    return j.dump();
}

SubmitResponse SubmitResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        SubmitResponse resp;
        
        if (!j.contains("task_id")) {
            throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                "SubmitResponse missing required field: task_id");
        }
        
        resp.task_id = j["task_id"].get<uint64_t>();
        
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse SubmitResponse JSON: ") + e.what());
    }
}

// ============================================================================
// DeleteResponse
// ============================================================================

std::string DeleteResponse::toJson() const {
    json j;
    j["results"] = results;
    return j.dump();
}

DeleteResponse DeleteResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        DeleteResponse resp;
        
        if (!j.contains("results")) {
            throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
                "DeleteResponse missing required field: results");
        }
        
        resp.results = j["results"].get<std::vector<bool>>();
        
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse DeleteResponse JSON: ") + e.what());
    }
}

// ============================================================================
// ErrorResponse
// ============================================================================

std::string ErrorResponse::toJson() const {
    json j;
    j["code"] = code;
    j["message"] = message;
    return j.dump();
}

ErrorResponse ErrorResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        ErrorResponse resp;
        
        resp.code = j.value("code", 0);
        resp.message = j.value("message", "");
        
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse ErrorResponse JSON: ") + e.what());
    }
}

// ============================================================================
// DeleteAllResponse
// ============================================================================

std::string DeleteAllResponse::toJson() const {
    json j;
    j["deleted_count"] = deleted_count;
    j["running_terminated"] = running_terminated;
    j["pending_deleted"] = pending_deleted;
    j["completed_deleted"] = completed_deleted;
    return j.dump();
}

DeleteAllResponse DeleteAllResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        DeleteAllResponse resp;
        resp.deleted_count = j.value("deleted_count", 0);
        resp.running_terminated = j.value("running_terminated", 0);
        resp.pending_deleted = j.value("pending_deleted", 0);
        resp.completed_deleted = j.value("completed_deleted", 0);
        
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse DeleteAllResponse JSON: ") + e.what());
    }
}

// ============================================================================
// TaskInfoRequest
// ============================================================================

std::string TaskInfoRequest::toJson() const {
    json j;
    j["task_id"] = task_id;
    return j.dump();
}

TaskInfoRequest TaskInfoRequest::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        TaskInfoRequest req;
        req.task_id = j.value("task_id", 0ULL);
        return req;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse TaskInfoRequest JSON: ") + e.what());
    }
}

// ============================================================================
// TaskDetailResponse
// ============================================================================

std::string TaskDetailResponse::toJson() const {
    json j;
    j["id"] = id;
    j["status"] = status;
    j["script"] = script;
    j["workdir"] = workdir;
    j["ncpu"] = ncpu;
    j["ngpu"] = ngpu;
    j["specific_cpus"] = specific_cpus;
    j["specific_gpus"] = specific_gpus;
    j["allocated_cpus"] = allocated_cpus;
    j["allocated_gpus"] = allocated_gpus;
    j["log_file"] = log_file;
    j["exit_code"] = exit_code;
    j["pid"] = pid;
    j["submit_time"] = submit_time;
    j["start_time"] = start_time;
    j["end_time"] = end_time;
    j["duration_seconds"] = duration_seconds;
    j["found"] = found;
    return j.dump();
}

TaskDetailResponse TaskDetailResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        TaskDetailResponse resp;
        resp.id = j.value("id", 0ULL);
        resp.status = j.value("status", "");
        resp.script = j.value("script", "");
        resp.workdir = j.value("workdir", "");
        resp.ncpu = j.value("ncpu", 0);
        resp.ngpu = j.value("ngpu", 0);
        if (j.contains("specific_cpus")) {
            resp.specific_cpus = j["specific_cpus"].get<std::vector<int>>();
        }
        if (j.contains("specific_gpus")) {
            resp.specific_gpus = j["specific_gpus"].get<std::vector<int>>();
        }
        if (j.contains("allocated_cpus")) {
            resp.allocated_cpus = j["allocated_cpus"].get<std::vector<int>>();
        }
        if (j.contains("allocated_gpus")) {
            resp.allocated_gpus = j["allocated_gpus"].get<std::vector<int>>();
        }
        resp.log_file = j.value("log_file", "");
        resp.exit_code = j.value("exit_code", 0);
        resp.pid = j.value("pid", 0);
        resp.submit_time = j.value("submit_time", "");
        resp.start_time = j.value("start_time", "");
        resp.end_time = j.value("end_time", "");
        resp.duration_seconds = j.value("duration_seconds", 0LL);
        resp.found = j.value("found", false);
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse TaskDetailResponse JSON: ") + e.what());
    }
}

// ============================================================================
// TaskLogRequest
// ============================================================================

std::string TaskLogRequest::toJson() const {
    json j;
    j["task_id"] = task_id;
    j["tail_lines"] = tail_lines;
    return j.dump();
}

TaskLogRequest TaskLogRequest::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        TaskLogRequest req;
        req.task_id = j.value("task_id", 0ULL);
        req.tail_lines = j.value("tail_lines", 0);
        return req;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse TaskLogRequest JSON: ") + e.what());
    }
}

// ============================================================================
// TaskLogResponse
// ============================================================================

std::string TaskLogResponse::toJson() const {
    json j;
    j["task_id"] = task_id;
    j["log_path"] = log_path;
    j["content"] = content;
    j["found"] = found;
    j["error"] = error;
    return j.dump();
}

TaskLogResponse TaskLogResponse::fromJson(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        TaskLogResponse resp;
        resp.task_id = j.value("task_id", 0ULL);
        resp.log_path = j.value("log_path", "");
        resp.content = j.value("content", "");
        resp.found = j.value("found", false);
        resp.error = j.value("error", "");
        return resp;
    } catch (const json::exception& e) {
        throw MyQueueException(ErrorCode::FILE_PARSE_ERROR,
            std::string("Failed to parse TaskLogResponse JSON: ") + e.what());
    }
}

} // namespace myqueue

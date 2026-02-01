/**
 * @file test_protocol.cpp
 * @brief Unit tests for IPC protocol message types and serialization
 */

#include <gtest/gtest.h>
#include "myqueue/protocol.h"
#include "myqueue/errors.h"

namespace myqueue {
namespace testing {

// ============================================================================
// MsgType Tests
// ============================================================================

class MsgTypeTest : public ::testing::Test {};

TEST_F(MsgTypeTest, MsgTypeToString) {
    EXPECT_EQ(msgTypeToString(MsgType::SUBMIT), "SUBMIT");
    EXPECT_EQ(msgTypeToString(MsgType::QUERY_QUEUE), "QUERY_QUEUE");
    EXPECT_EQ(msgTypeToString(MsgType::DELETE_TASK), "DELETE_TASK");
    EXPECT_EQ(msgTypeToString(MsgType::SHUTDOWN), "SHUTDOWN");
    EXPECT_EQ(msgTypeToString(MsgType::OK), "OK");
    EXPECT_EQ(msgTypeToString(MsgType::ERROR), "ERROR");
}

TEST_F(MsgTypeTest, MsgTypeFromString) {
    EXPECT_EQ(msgTypeFromString("SUBMIT"), MsgType::SUBMIT);
    EXPECT_EQ(msgTypeFromString("QUERY_QUEUE"), MsgType::QUERY_QUEUE);
    EXPECT_EQ(msgTypeFromString("DELETE_TASK"), MsgType::DELETE_TASK);
    EXPECT_EQ(msgTypeFromString("SHUTDOWN"), MsgType::SHUTDOWN);
    EXPECT_EQ(msgTypeFromString("OK"), MsgType::OK);
    EXPECT_EQ(msgTypeFromString("ERROR"), MsgType::ERROR);
}

TEST_F(MsgTypeTest, MsgTypeFromStringInvalid) {
    EXPECT_THROW(msgTypeFromString("INVALID"), std::invalid_argument);
    EXPECT_THROW(msgTypeFromString(""), std::invalid_argument);
    EXPECT_THROW(msgTypeFromString("submit"), std::invalid_argument);  // case sensitive
}

TEST_F(MsgTypeTest, MsgTypeEnumValues) {
    // Verify enum values match design spec
    EXPECT_EQ(static_cast<uint8_t>(MsgType::SUBMIT), 1);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::QUERY_QUEUE), 2);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::DELETE_TASK), 3);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::SHUTDOWN), 4);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::OK), 100);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::ERROR), 101);
}

// ============================================================================
// SubmitRequest Tests
// ============================================================================

class SubmitRequestTest : public ::testing::Test {
protected:
    void SetUp() override {
        sample_req_.script_path = "/home/user/job.sh";
        sample_req_.workdir = "/home/user/calc";
        sample_req_.ncpu = 4;
        sample_req_.ngpu = 2;
        sample_req_.specific_cpus = {0, 1, 2, 3};
        sample_req_.specific_gpus = {0, 1};
    }
    
    SubmitRequest sample_req_;
};

TEST_F(SubmitRequestTest, JsonRoundTrip) {
    std::string json = sample_req_.toJson();
    SubmitRequest restored = SubmitRequest::fromJson(json);
    
    EXPECT_EQ(restored.script_path, sample_req_.script_path);
    EXPECT_EQ(restored.workdir, sample_req_.workdir);
    EXPECT_EQ(restored.ncpu, sample_req_.ncpu);
    EXPECT_EQ(restored.ngpu, sample_req_.ngpu);
    EXPECT_EQ(restored.specific_cpus, sample_req_.specific_cpus);
    EXPECT_EQ(restored.specific_gpus, sample_req_.specific_gpus);
    EXPECT_EQ(restored, sample_req_);
}

TEST_F(SubmitRequestTest, JsonRoundTripMinimal) {
    SubmitRequest minimal;
    minimal.script_path = "test.sh";
    minimal.workdir = ".";
    // ncpu and ngpu use defaults
    
    std::string json = minimal.toJson();
    SubmitRequest restored = SubmitRequest::fromJson(json);
    
    EXPECT_EQ(restored.script_path, "test.sh");
    EXPECT_EQ(restored.workdir, ".");
    EXPECT_EQ(restored.ncpu, 1);
    EXPECT_EQ(restored.ngpu, 1);
    EXPECT_TRUE(restored.specific_cpus.empty());
    EXPECT_TRUE(restored.specific_gpus.empty());
}

TEST_F(SubmitRequestTest, JsonRoundTripEmptyArrays) {
    SubmitRequest req;
    req.script_path = "/path/to/script.sh";
    req.workdir = "/work";
    req.ncpu = 2;
    req.ngpu = 1;
    // specific_cpus and specific_gpus are empty
    
    std::string json = req.toJson();
    SubmitRequest restored = SubmitRequest::fromJson(json);
    
    EXPECT_EQ(restored, req);
    EXPECT_TRUE(restored.specific_cpus.empty());
    EXPECT_TRUE(restored.specific_gpus.empty());
}

TEST_F(SubmitRequestTest, InvalidJsonThrows) {
    EXPECT_THROW(SubmitRequest::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(SubmitRequest::fromJson("{}"), MyQueueException);
    EXPECT_THROW(SubmitRequest::fromJson("{\"script_path\": \"test.sh\"}"), MyQueueException);
}

TEST_F(SubmitRequestTest, EqualityOperator) {
    SubmitRequest copy = sample_req_;
    EXPECT_EQ(copy, sample_req_);
    
    copy.ncpu = 8;
    EXPECT_NE(copy, sample_req_);
}

// ============================================================================
// DeleteRequest Tests
// ============================================================================

class DeleteRequestTest : public ::testing::Test {
protected:
    void SetUp() override {
        sample_req_.task_ids = {1, 2, 3, 10, 100};
    }
    
    DeleteRequest sample_req_;
};

TEST_F(DeleteRequestTest, JsonRoundTrip) {
    std::string json = sample_req_.toJson();
    DeleteRequest restored = DeleteRequest::fromJson(json);
    
    EXPECT_EQ(restored.task_ids, sample_req_.task_ids);
    EXPECT_EQ(restored, sample_req_);
}

TEST_F(DeleteRequestTest, JsonRoundTripEmpty) {
    DeleteRequest empty;
    // task_ids is empty
    
    std::string json = empty.toJson();
    DeleteRequest restored = DeleteRequest::fromJson(json);
    
    EXPECT_TRUE(restored.task_ids.empty());
    EXPECT_EQ(restored, empty);
}

TEST_F(DeleteRequestTest, JsonRoundTripSingleId) {
    DeleteRequest single;
    single.task_ids = {42};
    
    std::string json = single.toJson();
    DeleteRequest restored = DeleteRequest::fromJson(json);
    
    EXPECT_EQ(restored.task_ids.size(), 1);
    EXPECT_EQ(restored.task_ids[0], 42);
}

TEST_F(DeleteRequestTest, InvalidJsonThrows) {
    EXPECT_THROW(DeleteRequest::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(DeleteRequest::fromJson("{}"), MyQueueException);
}

TEST_F(DeleteRequestTest, EqualityOperator) {
    DeleteRequest copy = sample_req_;
    EXPECT_EQ(copy, sample_req_);
    
    copy.task_ids.push_back(999);
    EXPECT_NE(copy, sample_req_);
}

// ============================================================================
// TaskInfo Tests
// ============================================================================

class TaskInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        sample_info_.id = 123;
        sample_info_.status = "running";
        sample_info_.script = "/home/user/job.sh";
        sample_info_.workdir = "/home/user/calc";
        sample_info_.cpus = {0, 1, 2, 3};
        sample_info_.gpus = {0, 1};
    }
    
    TaskInfo sample_info_;
};

TEST_F(TaskInfoTest, JsonRoundTrip) {
    std::string json = sample_info_.toJson();
    TaskInfo restored = TaskInfo::fromJson(json);
    
    EXPECT_EQ(restored.id, sample_info_.id);
    EXPECT_EQ(restored.status, sample_info_.status);
    EXPECT_EQ(restored.script, sample_info_.script);
    EXPECT_EQ(restored.workdir, sample_info_.workdir);
    EXPECT_EQ(restored.cpus, sample_info_.cpus);
    EXPECT_EQ(restored.gpus, sample_info_.gpus);
    EXPECT_EQ(restored, sample_info_);
}

TEST_F(TaskInfoTest, JsonRoundTripPending) {
    TaskInfo pending;
    pending.id = 456;
    pending.status = "pending";
    pending.script = "job.sh";
    pending.workdir = "/work";
    // cpus and gpus are empty for pending tasks
    
    std::string json = pending.toJson();
    TaskInfo restored = TaskInfo::fromJson(json);
    
    EXPECT_EQ(restored.id, 456);
    EXPECT_EQ(restored.status, "pending");
    EXPECT_TRUE(restored.cpus.empty());
    EXPECT_TRUE(restored.gpus.empty());
}

TEST_F(TaskInfoTest, InvalidJsonThrows) {
    EXPECT_THROW(TaskInfo::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(TaskInfo::fromJson("{}"), MyQueueException);
    EXPECT_THROW(TaskInfo::fromJson("{\"id\": 1}"), MyQueueException);
}

TEST_F(TaskInfoTest, EqualityOperator) {
    TaskInfo copy = sample_info_;
    EXPECT_EQ(copy, sample_info_);
    
    copy.status = "pending";
    EXPECT_NE(copy, sample_info_);
}

// ============================================================================
// QueueResponse Tests
// ============================================================================

class QueueResponseTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Add running tasks
        TaskInfo running1;
        running1.id = 1;
        running1.status = "running";
        running1.script = "/job1.sh";
        running1.workdir = "/work1";
        running1.cpus = {0, 1};
        running1.gpus = {0};
        sample_resp_.running.push_back(running1);
        
        TaskInfo running2;
        running2.id = 2;
        running2.status = "running";
        running2.script = "/job2.sh";
        running2.workdir = "/work2";
        running2.cpus = {2, 3};
        running2.gpus = {1};
        sample_resp_.running.push_back(running2);
        
        // Add pending tasks
        TaskInfo pending1;
        pending1.id = 3;
        pending1.status = "pending";
        pending1.script = "/job3.sh";
        pending1.workdir = "/work3";
        sample_resp_.pending.push_back(pending1);
    }
    
    QueueResponse sample_resp_;
};

TEST_F(QueueResponseTest, JsonRoundTrip) {
    std::string json = sample_resp_.toJson();
    QueueResponse restored = QueueResponse::fromJson(json);
    
    EXPECT_EQ(restored.running.size(), sample_resp_.running.size());
    EXPECT_EQ(restored.pending.size(), sample_resp_.pending.size());
    EXPECT_EQ(restored, sample_resp_);
}

TEST_F(QueueResponseTest, JsonRoundTripEmpty) {
    QueueResponse empty;
    
    std::string json = empty.toJson();
    QueueResponse restored = QueueResponse::fromJson(json);
    
    EXPECT_TRUE(restored.running.empty());
    EXPECT_TRUE(restored.pending.empty());
    EXPECT_EQ(restored, empty);
}

TEST_F(QueueResponseTest, JsonRoundTripOnlyRunning) {
    QueueResponse resp;
    TaskInfo running;
    running.id = 1;
    running.status = "running";
    running.script = "job.sh";
    running.workdir = "/work";
    running.cpus = {0};
    running.gpus = {0};
    resp.running.push_back(running);
    
    std::string json = resp.toJson();
    QueueResponse restored = QueueResponse::fromJson(json);
    
    EXPECT_EQ(restored.running.size(), 1);
    EXPECT_TRUE(restored.pending.empty());
}

TEST_F(QueueResponseTest, JsonRoundTripOnlyPending) {
    QueueResponse resp;
    TaskInfo pending;
    pending.id = 1;
    pending.status = "pending";
    pending.script = "job.sh";
    pending.workdir = "/work";
    resp.pending.push_back(pending);
    
    std::string json = resp.toJson();
    QueueResponse restored = QueueResponse::fromJson(json);
    
    EXPECT_TRUE(restored.running.empty());
    EXPECT_EQ(restored.pending.size(), 1);
}

TEST_F(QueueResponseTest, InvalidJsonThrows) {
    EXPECT_THROW(QueueResponse::fromJson("not valid json"), MyQueueException);
}

TEST_F(QueueResponseTest, EqualityOperator) {
    QueueResponse copy = sample_resp_;
    EXPECT_EQ(copy, sample_resp_);
    
    copy.running[0].id = 999;
    EXPECT_NE(copy, sample_resp_);
}

// ============================================================================
// SubmitResponse Tests
// ============================================================================

class SubmitResponseTest : public ::testing::Test {};

TEST_F(SubmitResponseTest, JsonRoundTrip) {
    SubmitResponse resp;
    resp.task_id = 12345;
    
    std::string json = resp.toJson();
    SubmitResponse restored = SubmitResponse::fromJson(json);
    
    EXPECT_EQ(restored.task_id, 12345);
    EXPECT_EQ(restored, resp);
}

TEST_F(SubmitResponseTest, JsonRoundTripZero) {
    SubmitResponse resp;
    resp.task_id = 0;
    
    std::string json = resp.toJson();
    SubmitResponse restored = SubmitResponse::fromJson(json);
    
    EXPECT_EQ(restored.task_id, 0);
}

TEST_F(SubmitResponseTest, JsonRoundTripLargeId) {
    SubmitResponse resp;
    resp.task_id = 18446744073709551615ULL;  // max uint64_t
    
    std::string json = resp.toJson();
    SubmitResponse restored = SubmitResponse::fromJson(json);
    
    EXPECT_EQ(restored.task_id, 18446744073709551615ULL);
}

TEST_F(SubmitResponseTest, InvalidJsonThrows) {
    EXPECT_THROW(SubmitResponse::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(SubmitResponse::fromJson("{}"), MyQueueException);
}

TEST_F(SubmitResponseTest, EqualityOperator) {
    SubmitResponse resp1;
    resp1.task_id = 100;
    
    SubmitResponse resp2;
    resp2.task_id = 100;
    
    EXPECT_EQ(resp1, resp2);
    
    resp2.task_id = 200;
    EXPECT_NE(resp1, resp2);
}

// ============================================================================
// DeleteResponse Tests
// ============================================================================

class DeleteResponseTest : public ::testing::Test {};

TEST_F(DeleteResponseTest, JsonRoundTrip) {
    DeleteResponse resp;
    resp.results = {true, false, true, true, false};
    
    std::string json = resp.toJson();
    DeleteResponse restored = DeleteResponse::fromJson(json);
    
    EXPECT_EQ(restored.results, resp.results);
    EXPECT_EQ(restored, resp);
}

TEST_F(DeleteResponseTest, JsonRoundTripEmpty) {
    DeleteResponse resp;
    // results is empty
    
    std::string json = resp.toJson();
    DeleteResponse restored = DeleteResponse::fromJson(json);
    
    EXPECT_TRUE(restored.results.empty());
}

TEST_F(DeleteResponseTest, JsonRoundTripAllTrue) {
    DeleteResponse resp;
    resp.results = {true, true, true};
    
    std::string json = resp.toJson();
    DeleteResponse restored = DeleteResponse::fromJson(json);
    
    EXPECT_EQ(restored.results.size(), 3);
    for (bool r : restored.results) {
        EXPECT_TRUE(r);
    }
}

TEST_F(DeleteResponseTest, JsonRoundTripAllFalse) {
    DeleteResponse resp;
    resp.results = {false, false, false};
    
    std::string json = resp.toJson();
    DeleteResponse restored = DeleteResponse::fromJson(json);
    
    EXPECT_EQ(restored.results.size(), 3);
    for (bool r : restored.results) {
        EXPECT_FALSE(r);
    }
}

TEST_F(DeleteResponseTest, InvalidJsonThrows) {
    EXPECT_THROW(DeleteResponse::fromJson("not valid json"), MyQueueException);
    EXPECT_THROW(DeleteResponse::fromJson("{}"), MyQueueException);
}

TEST_F(DeleteResponseTest, EqualityOperator) {
    DeleteResponse resp1;
    resp1.results = {true, false};
    
    DeleteResponse resp2;
    resp2.results = {true, false};
    
    EXPECT_EQ(resp1, resp2);
    
    resp2.results = {false, true};
    EXPECT_NE(resp1, resp2);
}

// ============================================================================
// ErrorResponse Tests
// ============================================================================

class ErrorResponseTest : public ::testing::Test {};

TEST_F(ErrorResponseTest, JsonRoundTrip) {
    ErrorResponse resp;
    resp.code = 404;
    resp.message = "Task not found";
    
    std::string json = resp.toJson();
    ErrorResponse restored = ErrorResponse::fromJson(json);
    
    EXPECT_EQ(restored.code, 404);
    EXPECT_EQ(restored.message, "Task not found");
    EXPECT_EQ(restored, resp);
}

TEST_F(ErrorResponseTest, JsonRoundTripEmptyMessage) {
    ErrorResponse resp;
    resp.code = 500;
    resp.message = "";
    
    std::string json = resp.toJson();
    ErrorResponse restored = ErrorResponse::fromJson(json);
    
    EXPECT_EQ(restored.code, 500);
    EXPECT_EQ(restored.message, "");
}

TEST_F(ErrorResponseTest, JsonRoundTripZeroCode) {
    ErrorResponse resp;
    resp.code = 0;
    resp.message = "Success";
    
    std::string json = resp.toJson();
    ErrorResponse restored = ErrorResponse::fromJson(json);
    
    EXPECT_EQ(restored.code, 0);
    EXPECT_EQ(restored.message, "Success");
}

TEST_F(ErrorResponseTest, InvalidJsonThrows) {
    EXPECT_THROW(ErrorResponse::fromJson("not valid json"), MyQueueException);
}

TEST_F(ErrorResponseTest, JsonWithMissingFieldsUsesDefaults) {
    // ErrorResponse allows missing fields with defaults
    ErrorResponse restored = ErrorResponse::fromJson("{}");
    EXPECT_EQ(restored.code, 0);
    EXPECT_EQ(restored.message, "");
}

TEST_F(ErrorResponseTest, EqualityOperator) {
    ErrorResponse resp1;
    resp1.code = 100;
    resp1.message = "Error";
    
    ErrorResponse resp2;
    resp2.code = 100;
    resp2.message = "Error";
    
    EXPECT_EQ(resp1, resp2);
    
    resp2.message = "Different error";
    EXPECT_NE(resp1, resp2);
}

} // namespace testing
} // namespace myqueue

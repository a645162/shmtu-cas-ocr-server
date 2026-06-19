#pragma once

#include <shmtu/cas_ocr/cas_ocr.h>

#include <memory>
#include <string>
#include <chrono>

namespace shmtu::cas::ocr {

struct ServerConfig {
    std::string http_host = "0.0.0.0";
    int http_port = 21600;

    std::string tcp_host = "0.0.0.0";
    int tcp_port = 21601;

    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;
    ModelVersion model_version = ModelVersion::V2;  // V2 is the new default.

    int worker_count = 0;
    int queue_capacity = 0;
    int inference_threads = 0;
    std::string log_dir = "./logs";
    std::string log_file_prefix = "shmtu_cas_ocr_server";
    int log_min_level = 0;
    bool log_to_stderr = false;
    bool also_log_to_stderr = true;
    int log_max_size_mb = 10;
    int log_cleanup_interval_secs = 3600;
    int log_retention_days = 7;

    /// Model download source: "gitee" (default) or "github".
    /// Controls which mirror is tried first when auto-downloading v2 models.
    /// Set via SHMTU_MODEL_SOURCE env var or --model-source CLI option.
    std::string model_source = "gitee";

    std::string server_name;
};

struct ServerStats {
    int total_requests = 0;
    int successful_requests = 0;
    int failed_requests = 0;
    int active_workers = 0;
    int pool_size = 0;
    int queue_capacity = 0;
    int pending_requests = 0;
    bool models_loaded = false;
    std::chrono::steady_clock::time_point start_time;
    std::string server_name;
};

struct HealthResult {
    std::string status;
    int availability_level = 0;
    std::string reason;
    bool models_loaded = false;
    int pool_size = 0;
    int queue_capacity = 0;
    int pending_requests = 0;
    std::string server_name;
};

class OcrServer {
public:
    struct Impl;

    explicit OcrServer(const ServerConfig& config);
    ~OcrServer();

    OcrServer(const OcrServer&) = delete;
    OcrServer& operator=(const OcrServer&) = delete;

    int run();
    void stop();

    HealthResult health() const;
    ServerStats stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace shmtu::cas::ocr

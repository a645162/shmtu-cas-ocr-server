#pragma once

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

    int worker_count = 2;
    int queue_capacity = 16;
    int log_level = 2;
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
};

struct HealthResult {
    std::string status;
    int availability_level = 0;
    std::string reason;
    bool models_loaded = false;
    int pool_size = 0;
    int queue_capacity = 0;
    int pending_requests = 0;
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

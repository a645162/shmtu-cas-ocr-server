#pragma once

#include "cas_ocr.h"
#include "types.h"

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace shmtu::cas_ocr {

// Configuration for the OCR server.
struct ServerConfig {
    // HTTP (RESTful API) settings
    std::string http_host = "0.0.0.0";
    int http_port = 21600;

    // TCP (legacy protocol) settings
    std::string tcp_host = "0.0.0.0";
    int tcp_port = 21601;

    // Model settings
    std::string model_dir = "./models";
    std::string precision = "fp16";
    bool use_gpu = false;

    // Worker pool settings
    int worker_count = 2;  // Number of CasOcr instances

    // Request queue capacity (back-pressure when all workers busy)
    int queue_capacity = 16;

    // Log level: 0=trace, 1=debug, 2=info, 3=warn, 4=error
    int log_level = 2;
};

// Runtime statistics for the server.
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

// Health check result.
struct HealthResult {
    std::string status;             // "ok", "degraded", "unavailable"
    int availability_level = 0;     // 0=unavailable, 1=degraded, 2=ok
    std::string reason;
    bool models_loaded = false;
    int pool_size = 0;
    int queue_capacity = 0;
    int pending_requests = 0;
};

// The main server class. Owns worker pool, HTTP server, and TCP server.
class OcrServer {
public:
    explicit OcrServer(const ServerConfig& config);
    ~OcrServer();

    OcrServer(const OcrServer&) = delete;
    OcrServer& operator=(const OcrServer&) = delete;

    // Initialize models and start HTTP + TCP servers.
    // Blocks until shutdown is requested (via stop() or signal).
    int run();

    // Request graceful shutdown.
    void stop();

    // Query current health / stats.
    HealthResult health() const;
    ServerStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace shmtu::cas_ocr

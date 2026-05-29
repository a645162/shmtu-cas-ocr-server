#pragma once

#include <shmtu/cas_ocr/server.h>
#include <shmtu/cas_ocr/base64.h>
#include <shmtu/cas_ocr/cas_ocr.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <expected>

#include <drogon/HttpResponse.h>
#include <drogon/HttpRequest.h>
#include <trantor/net/EventLoopThread.h>
#include <trantor/net/TcpConnection.h>
#include <trantor/net/TcpServer.h>

namespace shmtu::cas::ocr {

drogon::HttpResponsePtr make_json_response(
    const Json::Value& body,
    drogon::HttpStatusCode status = drogon::k200OK);

Json::Value predict_result_to_json(const PredictResult& result);
Json::Value health_to_json(const HealthResult& health);
std::string describe_http_client(const drogon::HttpRequestPtr& req);
void init_server_logging(const ServerConfig& config, const char* argv0);
void shutdown_server_logging();
void cleanup_old_log_files(const ServerConfig& config);

struct PredictExecutionInfo {
    size_t input_bytes = 0;
    size_t worker_index = 0;
    std::chrono::microseconds queue_wait{0};
    std::chrono::microseconds inference_time{0};
    std::chrono::microseconds total_time{0};
};

struct OcrWorkerPool {
    std::vector<std::unique_ptr<CasOcr>> workers;
    std::counting_semaphore<> semaphore{0};
    std::mutex queue_mutex;
    std::queue<std::function<void(CasOcr&, size_t)>> task_queue;
    std::vector<std::jthread> threads;
    std::atomic<bool> accepting{true};
    std::atomic<int> active_workers{0};
    std::atomic<int> pending_tasks{0};

    void add_worker(std::unique_ptr<CasOcr> ocr);
    void start();
    bool submit(std::function<void(CasOcr&, size_t)> task, int max_queue);
    void stop_all();
};

struct OcrServer::Impl {
    explicit Impl(const ServerConfig& cfg);

    bool submit_predict(std::span<const uint8_t> image_bytes,
                        std::function<void(PredictResult, PredictExecutionInfo)> on_complete);
    bool submit_predict(std::vector<uint8_t> image_bytes,
                        std::function<void(PredictResult, PredictExecutionInfo)> on_complete);

    ServerConfig config;
    std::unique_ptr<OcrWorkerPool> pool;
    std::atomic<bool> running{false};
    std::atomic<bool> stopping{false};
    std::atomic<int> total_requests{0};
    std::atomic<int> successful_requests{0};
    std::atomic<int> failed_requests{0};
    std::chrono::steady_clock::time_point start_time{};

    std::unique_ptr<trantor::EventLoopThread> tcp_loop_thread;
    std::unique_ptr<trantor::TcpServer> tcp_server;
    std::mutex tcp_buffer_mutex;
    std::unordered_map<trantor::TcpConnectionPtr, std::string> tcp_buffers;
    std::atomic<unsigned long long> request_sequence{0};
    std::jthread log_cleanup_thread;
};

void register_http_handlers(OcrServer::Impl& impl, OcrServer& server);
void start_tcp_server(OcrServer::Impl& impl);
void stop_tcp_server(OcrServer::Impl& impl);

} // namespace shmtu::cas::ocr

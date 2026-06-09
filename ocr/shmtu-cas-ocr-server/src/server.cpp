#include <shmtu/cas_ocr/server.h>

#include "server_internal.h"
#include "logging.h"

#include <cstdio>
#include <exception>
#include <memory>

#include <drogon/HttpAppFramework.h>

namespace shmtu::cas::ocr {

OcrServer::OcrServer(const ServerConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

OcrServer::~OcrServer() {
    stop();
}

int OcrServer::run() {
    auto& cfg = impl_->config;
    LOG(INFO) << "OcrServer::run entered";
    impl_->start_time = std::chrono::steady_clock::now();
    impl_->running.store(true);
    impl_->stopping.store(false);
    impl_->pool = std::make_unique<OcrWorkerPool>();
    LOG(INFO) << "Worker pool allocated";

    cleanup_old_log_files(cfg);
    if (cfg.log_cleanup_interval_secs > 0 && cfg.log_retention_days > 0) {
        impl_->log_cleanup_thread = std::jthread([cfg](std::stop_token stop_token) {
            LOG(INFO) << "Log cleanup thread started"
                      << " interval_secs=" << cfg.log_cleanup_interval_secs
                      << " retention_days=" << cfg.log_retention_days;
            while (!stop_token.stop_requested()) {
                for (int i = 0; i < cfg.log_cleanup_interval_secs && !stop_token.stop_requested(); ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (stop_token.stop_requested()) {
                    break;
                }
                cleanup_old_log_files(cfg);
            }
            LOG(INFO) << "Log cleanup thread stopped";
        });
    }

    std::printf("Loading models from: %s (precision=%s, gpu=%s, version=%s)\n",
                cfg.model_dir.c_str(),
                cfg.precision.c_str(),
                cfg.use_gpu ? "true" : "false",
                model_version_to_string(cfg.model_version).c_str());
    std::printf("Runtime tuning: workers=%d, ncnn_threads=%d, queue_capacity=%d\n",
                cfg.worker_count,
                cfg.inference_threads,
                cfg.queue_capacity);

    for (int i = 0; i < cfg.worker_count; ++i) {
        LOG(INFO) << "Initializing worker " << i << " of " << cfg.worker_count;
        auto ocr = std::make_unique<CasOcr>(cfg.model_dir);
#ifdef NCNN_SUPPORT_VULKAN
        if (cfg.use_gpu && CasOcr::gpu_count() > 0) {
            const auto gpu = CasOcr::gpu_info(i % CasOcr::gpu_count());
            std::printf("GPU device %d: %s\n", gpu.device_index, gpu.device_name.c_str());
            LOG(INFO) << "Worker " << i << " selecting GPU device "
                      << gpu.device_index << " (" << gpu.device_name << ")";
        }
#endif
        if (!ocr->load_model(cfg.precision, cfg.use_gpu, cfg.inference_threads, cfg.model_version)) {
            std::fprintf(stderr, "Failed to load model for worker %d\n", i);
            LOG(ERROR) << "Failed to load model for worker " << i;
            return -1;
        }

        std::printf("Worker %d: model loaded (%s)\n",
                    i,
                    ocr->model_status() == ModelStatus::LoadedGPU ? "GPU" : "CPU");
        LOG(INFO) << "Worker " << i << " model loaded successfully";
        impl_->pool->add_worker(std::move(ocr));
    }

    impl_->pool->start();
    std::printf("All %d worker(s) initialized.\n", cfg.worker_count);
    LOG(INFO) << "All workers initialized";

    LOG(INFO) << "Registering HTTP handlers";
    register_http_handlers(*impl_, *this);
    LOG(INFO) << "Starting TCP server";
    start_tcp_server(*impl_);

    std::printf("HTTP server starting on %s:%d\n",
                cfg.http_host.c_str(),
                cfg.http_port);
    LOG(INFO) << "Starting Drogon event loop on "
              << cfg.http_host << ":" << cfg.http_port;

    try {
        drogon::app().run();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Server runtime error: %s\n", ex.what());
        LOG(ERROR) << "Server runtime error: " << ex.what();
        stop();
        return -1;
    }

    LOG(INFO) << "Drogon event loop exited";
    return 0;
}

void OcrServer::stop() {
    if (!impl_ || impl_->stopping.exchange(true)) {
        return;
    }

    LOG(INFO) << "OcrServer::stop requested";
    impl_->running.store(false);
    drogon::app().quit();
    stop_tcp_server(*impl_);

    if (impl_->pool) {
        impl_->pool->stop_all();
    }
    if (impl_->log_cleanup_thread.joinable()) {
        impl_->log_cleanup_thread.request_stop();
        impl_->log_cleanup_thread.join();
    }
    LOG(INFO) << "OcrServer::stop completed";
}

HealthResult OcrServer::health() const {
    HealthResult health;
    const bool loaded = impl_->pool && !impl_->pool->workers.empty() &&
                        impl_->pool->workers.front()->is_loaded();

    health.models_loaded = loaded;
    health.pool_size = impl_->pool ? static_cast<int>(impl_->pool->workers.size()) : 0;
    health.queue_capacity = impl_->config.queue_capacity;
    health.pending_requests = impl_->pool ? impl_->pool->pending_tasks.load() : 0;

    if (!loaded) {
        health.status = "unavailable";
        health.availability_level = 0;
        health.reason = "Models not loaded";
    } else if (health.pending_requests > health.queue_capacity / 2) {
        health.status = "degraded";
        health.availability_level = 1;
        health.reason = "High load";
    } else {
        health.status = "healthy";
        health.availability_level = 2;
    }

    health.server_name = impl_->config.server_name;

    return health;
}

ServerStats OcrServer::stats() const {
    ServerStats stats;
    stats.total_requests = impl_->total_requests.load();
    stats.successful_requests = impl_->successful_requests.load();
    stats.failed_requests = impl_->failed_requests.load();
    stats.active_workers = impl_->pool ? impl_->pool->active_workers.load() : 0;
    stats.pool_size = impl_->pool ? static_cast<int>(impl_->pool->workers.size()) : 0;
    stats.queue_capacity = impl_->config.queue_capacity;
    stats.pending_requests = impl_->pool ? impl_->pool->pending_tasks.load() : 0;
    stats.models_loaded = impl_->pool && !impl_->pool->workers.empty() &&
                          impl_->pool->workers.front()->is_loaded();
    stats.start_time = impl_->start_time;
    stats.server_name = impl_->config.server_name;
    return stats;
}

} // namespace shmtu::cas::ocr

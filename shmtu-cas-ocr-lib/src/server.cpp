#include <shmtu/cas_ocr/server.h>
#include <shmtu/cas_ocr/cas_ocr.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <queue>
#include <semaphore>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// cpp-httplib (header-only)
#include <httplib.h>

// Poco TCP
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>

// OpenCV (for image decode in TCP handler)
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// Base64 decoding
#include <algorithm>
#include <cctype>

namespace shmtu::cas_ocr {

// ---------------------------------------------------------------------------
// Simple base64 decoder
// ---------------------------------------------------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<uint8_t> base64_decode(std::string_view input) {
    // Build decode lookup
    static std::array<int, 256> decode_table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i) {
            t[static_cast<unsigned char>(kBase64Table[i])] = i;
        }
        return t;
    }();

    std::vector<uint8_t> result;
    result.reserve(input.size() * 3 / 4);

    int val = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (decode_table[c] == -1) {
            if (c == '=') break; // padding
            continue;            // skip whitespace etc.
        }
        val = (val << 6) | decode_table[c];
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Worker Pool — manages multiple CasOcr instances
// ---------------------------------------------------------------------------

struct OcrWorkerPool {
    std::vector<std::unique_ptr<CasOcr>> workers;
    std::counting_semaphore<> semaphore;
    std::mutex queue_mutex;
    std::queue<std::function<void(CasOcr&)>> task_queue;
    std::vector<std::jthread> threads;
    std::atomic<int> active_workers{0};
    std::atomic<int> pending_tasks{0};

    explicit OcrWorkerPool(int count)
        : semaphore(0) {
        (void)count; // workers added via add_worker()
    }

    void add_worker(std::unique_ptr<CasOcr> ocr) {
        workers.push_back(std::move(ocr));
    }

    // Start worker threads — each thread owns a CasOcr instance,
    // waits on the semaphore, picks up a task, and executes it.
    void start() {
        for (size_t i = 0; i < workers.size(); ++i) {
            threads.emplace_back([this, idx = i](std::stop_token st) {
                while (!st.stop_requested()) {
                    // Wait for a task signal
                    if (!semaphore.try_acquire_for(std::chrono::milliseconds(100))) {
                        continue;
                    }

                    std::function<void(CasOcr&)> task;
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        if (task_queue.empty()) continue;
                        task = std::move(task_queue.front());
                        task_queue.pop();
                        pending_tasks.fetch_sub(1);
                    }

                    active_workers.fetch_add(1);
                    task(*workers[idx]);
                    active_workers.fetch_sub(1);
                }
            });
        }
    }

    // Submit a task and signal a worker.
    // Returns false if queue is full (back-pressure).
    bool submit(std::function<void(CasOcr&)> task, int max_queue) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (static_cast<int>(task_queue.size()) >= max_queue) {
                return false;
            }
            task_queue.push(std::move(task));
            pending_tasks.fetch_add(1);
        }
        semaphore.release();
        return true;
    }

    void stop_all() {
        for (auto& t : threads) {
            t.request_stop();
        }
        // Release enough semaphore permits to unblock all workers
        semaphore.release(static_cast<ptrdiff_t>(workers.size()));
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
        threads.clear();
    }
};

// ---------------------------------------------------------------------------
// JSON helpers (minimal, no external JSON lib dependency)
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

static std::string predict_result_to_json(const PredictResult& r) {
    std::string json = "{";
    json += "\"success\":" + std::string(r.success ? "true" : "false") + ",";
    json += "\"expression\":\"" + json_escape(r.expression) + "\",";
    json += "\"result\":" + std::to_string(r.result) + ",";
    json += "\"equalSymbol\":" + std::to_string(r.equal_symbol) + ",";
    json += "\"operator\":" + std::to_string(r.op) + ",";
    json += "\"digit1\":" + std::to_string(r.digit1) + ",";
    json += "\"digit2\":" + std::to_string(r.digit2);
    if (!r.error.empty()) {
        json += ",\"error\":\"" + json_escape(r.error) + "\"";
    }
    json += "}";
    return json;
}

static std::string health_to_json(const HealthResult& h) {
    std::string json = "{";
    json += "\"status\":\"" + json_escape(h.status) + "\",";
    json += "\"availabilityLevel\":" + std::to_string(h.availability_level) + ",";
    json += "\"reason\":\"" + json_escape(h.reason) + "\",";
    json += "\"modelsLoaded\":" + std::string(h.models_loaded ? "true" : "false") + ",";
    json += "\"poolSize\":" + std::to_string(h.pool_size) + ",";
    json += "\"queueCapacity\":" + std::to_string(h.queue_capacity) + ",";
    json += "\"pendingRequests\":" + std::to_string(h.pending_requests);
    json += "}";
    return json;
}

// ---------------------------------------------------------------------------
// OcrServer::Impl
// ---------------------------------------------------------------------------

struct OcrServer::Impl {
    ServerConfig config;
    std::unique_ptr<OcrWorkerPool> pool;
    std::atomic<bool> running{false};
    std::jthread http_thread;
    std::jthread tcp_thread;
    httplib::Server* http_server = nullptr;  // Non-owning; for graceful shutdown

    // Stats (atomic for lock-free reads)
    std::atomic<int> total_requests{0};
    std::atomic<int> successful_requests{0};
    std::atomic<int> failed_requests{0};
    std::chrono::steady_clock::time_point start_time;

    explicit Impl(const ServerConfig& cfg) : config(cfg) {}
};

// ---------------------------------------------------------------------------
// OcrServer
// ---------------------------------------------------------------------------

OcrServer::OcrServer(const ServerConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

OcrServer::~OcrServer() {
    stop();
}

int OcrServer::run() {
    auto& cfg = impl_->config;
    impl_->start_time = std::chrono::steady_clock::now();
    impl_->running.store(true);

    // --- Initialize worker pool ---
    impl_->pool = std::make_unique<OcrWorkerPool>(cfg.worker_count);

    printf("Loading models from: %s (precision=%s, gpu=%s)\n",
           cfg.model_dir.c_str(), cfg.precision.c_str(),
           cfg.use_gpu ? "true" : "false");

    for (int i = 0; i < cfg.worker_count; ++i) {
        auto ocr = std::make_unique<CasOcr>(cfg.model_dir, cfg.use_gpu);
#ifdef NCNN_SUPPORT_VULKAN
        if (cfg.use_gpu) {
            printf("GPU device %d: %s\n", i,
                   CasOcr::gpu_info(i % CasOcr::gpu_count()).device_name.c_str());
        }
#endif
        if (!ocr->load_model(cfg.precision)) {
            fprintf(stderr, "Failed to load model for worker %d\n", i);
            return -1;
        }
        printf("Worker %d: model loaded (%s)\n", i,
               ocr->model_status() == ModelStatus::LoadedGPU ? "GPU" : "CPU");
        impl_->pool->add_worker(std::move(ocr));
    }

    // Start the worker pool threads
    impl_->pool->start();

    printf("All %d worker(s) initialized.\n", cfg.worker_count);

    // --- Start HTTP (RESTful) server ---
    impl_->http_thread = std::jthread([&](std::stop_token st) {
        httplib::Server svr;

        // Store pointer for graceful shutdown
        impl_->http_server = &svr;

        // GET /api/health
        svr.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
            auto h = health();
            res.set_content(health_to_json(h), "application/json");
        });

        // POST /api/ocr — base64 image
        svr.Post("/api/ocr", [this](const httplib::Request& req, httplib::Response& res) {
            impl_->total_requests.fetch_add(1);

            // Expect JSON body: {"imageBase64": "..."}
            auto it = req.body.find("\"imageBase64\"");
            if (it == std::string::npos) {
                res.status = 400;
                res.set_content("{\"success\":false,\"error\":\"Missing imageBase64 field\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }

            // Extract base64 value (simple extraction between quotes)
            auto val_start = req.body.find('"', it + 14);
            if (val_start == std::string::npos) {
                res.status = 400;
                res.set_content("{\"success\":false,\"error\":\"Invalid imageBase64 format\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }
            auto val_end = req.body.find('"', val_start + 1);
            if (val_end == std::string::npos) {
                res.status = 400;
                res.set_content("{\"success\":false,\"error\":\"Invalid imageBase64 format\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }
            std::string_view b64_data(
                req.body.data() + val_start + 1,
                val_end - val_start - 1
            );

            auto image_bytes = base64_decode(b64_data);
            if (image_bytes.empty()) {
                res.status = 400;
                res.set_content("{\"success\":false,\"error\":\"Empty base64 data\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }

            // Submit to worker pool (synchronous wait for result)
            PredictResult result;
            std::mutex result_mutex;
            std::condition_variable result_cv;
            bool done = false;

            bool queued = impl_->pool->submit([&](CasOcr& ocr) {
                result = ocr.predict(image_bytes);
                {
                    std::lock_guard<std::mutex> lk(result_mutex);
                    done = true;
                }
                result_cv.notify_one();
            }, impl_->config.queue_capacity);

            if (!queued) {
                res.status = 503;
                res.set_content("{\"success\":false,\"error\":\"Server overloaded\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }

            // Wait for result (with timeout)
            {
                std::unique_lock<std::mutex> lk(result_mutex);
                if (!result_cv.wait_for(lk, std::chrono::seconds(30),
                                        [&] { return done; })) {
                    res.status = 504;
                    res.set_content("{\"success\":false,\"error\":\"Prediction timeout\"}",
                                    "application/json");
                    impl_->failed_requests.fetch_add(1);
                    return;
                }
            }

            if (result.success) {
                impl_->successful_requests.fetch_add(1);
            } else {
                impl_->failed_requests.fetch_add(1);
            }

            res.set_content(predict_result_to_json(result), "application/json");
        });

        // POST /api/ocr/upload — multipart file upload
        svr.Post("/api/ocr/upload", [this](const httplib::Request& req, httplib::Response& res) {
            impl_->total_requests.fetch_add(1);

            if (!req.has_file("file")) {
                res.status = 400;
                res.set_content("{\"success\":false,\"error\":\"No file uploaded\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }

            const auto& file = req.get_file_value("file");
            std::vector<uint8_t> image_data(file.content.begin(), file.content.end());

            PredictResult result;
            std::mutex result_mutex;
            std::condition_variable result_cv;
            bool done = false;

            bool queued = impl_->pool->submit([&](CasOcr& ocr) {
                result = ocr.predict(image_data);
                {
                    std::lock_guard<std::mutex> lk(result_mutex);
                    done = true;
                }
                result_cv.notify_one();
            }, impl_->config.queue_capacity);

            if (!queued) {
                res.status = 503;
                res.set_content("{\"success\":false,\"error\":\"Server overloaded\"}",
                                "application/json");
                impl_->failed_requests.fetch_add(1);
                return;
            }

            {
                std::unique_lock<std::mutex> lk(result_mutex);
                if (!result_cv.wait_for(lk, std::chrono::seconds(30),
                                        [&] { return done; })) {
                    res.status = 504;
                    res.set_content("{\"success\":false,\"error\":\"Prediction timeout\"}",
                                    "application/json");
                    impl_->failed_requests.fetch_add(1);
                    return;
                }
            }

            if (result.success) {
                impl_->successful_requests.fetch_add(1);
            } else {
                impl_->failed_requests.fetch_add(1);
            }

            res.set_content(predict_result_to_json(result), "application/json");
        });

        printf("HTTP server starting on %s:%d\n",
               cfg.http_host.c_str(), cfg.http_port);

        // svr.listen() blocks until svr.stop() is called
        if (!svr.listen(cfg.http_host, cfg.http_port)) {
            fprintf(stderr, "Failed to start HTTP server on %s:%d\n",
                    cfg.http_host.c_str(), cfg.http_port);
        }

        impl_->http_server = nullptr;
    });

    // --- Start TCP (legacy) server ---
    impl_->tcp_thread = std::jthread([&](std::stop_token st) {
        try {
            Poco::Net::ServerSocket srv;
            Poco::Net::SocketAddress addr(cfg.tcp_host, cfg.tcp_port);
            srv.bind(addr);
            srv.listen();

            printf("TCP server starting on %s:%d\n",
                   cfg.tcp_host.c_str(), cfg.tcp_port);

            while (!st.stop_requested()) {
                Poco::Net::StreamSocket client;
                try {
                    Poco::Timespan timeout(1000000); // 1 second
                    if (!srv.poll(timeout, Poco::Net::Socket::SELECT_READ)) {
                        continue;
                    }
                    client = srv.acceptConnection();
                } catch (...) {
                    continue;
                }

                const std::string peer = client.peerAddress().toString();
                printf("[TCP] Connection from: %s\n", peer.c_str());

                // Handle in a std::thread (fire-and-forget for TCP handler).
                // The thread self-manages the socket lifecycle.
                std::thread([this, client = std::move(client), peer]() mutable {
                    const std::string end_marker = "<END>";
                    std::string image_data;

                    while (true) {
                        try {
                            char buffer[4096];
                            int bytes = client.receiveBytes(buffer, sizeof(buffer));
                            if (bytes <= 0) break;
                            image_data.append(buffer, bytes);
                        } catch (Poco::Exception&) {
                            break;
                        }

                        if (image_data.size() >= end_marker.size() &&
                            image_data.compare(
                                image_data.size() - end_marker.size(),
                                end_marker.size(), end_marker) == 0) {
                            image_data.erase(image_data.size() - end_marker.size());
                            break;
                        }
                    }

                    printf("[TCP][%s] Received %zu bytes\n", peer.c_str(), image_data.size());

                    // Decode image via worker pool
                    std::vector<uint8_t> img_bytes(image_data.begin(), image_data.end());
                    PredictResult result;
                    std::mutex result_mutex;
                    std::condition_variable result_cv;
                    bool done = false;

                    bool queued = impl_->pool->submit([&](CasOcr& ocr) {
                        result = ocr.predict(img_bytes);
                        {
                            std::lock_guard<std::mutex> lk(result_mutex);
                            done = true;
                        }
                        result_cv.notify_one();
                    }, impl_->config.queue_capacity);

                    if (!queued) {
                        try { client.sendBytes("ERROR:OVERLOADED", 16); } catch (...) {}
                        client.close();
                        return;
                    }

                    {
                        std::unique_lock<std::mutex> lk(result_mutex);
                        result_cv.wait_for(lk, std::chrono::seconds(30),
                                           [&] { return done; });
                    }

                    impl_->total_requests.fetch_add(1);
                    if (result.success) {
                        impl_->successful_requests.fetch_add(1);
                    } else {
                        impl_->failed_requests.fetch_add(1);
                    }

                    // Send expression string back (legacy protocol)
                    try {
                        client.sendBytes(
                            result.expression.c_str(),
                            static_cast<int>(result.expression.size())
                        );
                    } catch (Poco::Exception& ex) {
                        fprintf(stderr, "[TCP][%s] Send error: %s\n",
                                peer.c_str(), ex.displayText().c_str());
                    }

                    client.close();
                    printf("[TCP][%s] %s\n", peer.c_str(), result.expression.c_str());
                }).detach();
            }

            srv.close();
        } catch (Poco::Exception& e) {
            fprintf(stderr, "TCP server error: %s\n", e.displayText().c_str());
        }
    });

    // --- Wait for shutdown signal ---
    // Block until stop() is called (e.g., from signal handler)
    while (impl_->running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // --- Graceful shutdown ---
    printf("Shutting down...\n");

    // Stop HTTP server (unblocks svr.listen())
    if (impl_->http_server) {
        impl_->http_server->stop();
    }

    if (impl_->pool) {
        impl_->pool->stop_all();
    }

    // Request stop on threads and join them
    impl_->http_thread.request_stop();
    impl_->tcp_thread.request_stop();

    // jthread destructors will join
    impl_->http_thread = {};
    impl_->tcp_thread = {};

    printf("Server stopped.\n");
    return 0;
}

void OcrServer::stop() {
    impl_->running.store(false);
}

HealthResult OcrServer::health() const {
    HealthResult h;
    bool loaded = impl_->pool && !impl_->pool->workers.empty() &&
                  impl_->pool->workers[0]->is_loaded();

    h.models_loaded = loaded;
    h.pool_size = impl_->pool ? static_cast<int>(impl_->pool->workers.size()) : 0;
    h.queue_capacity = impl_->config.queue_capacity;
    h.pending_requests = impl_->pool ? impl_->pool->pending_tasks.load() : 0;

    if (!loaded) {
        h.status = "unavailable";
        h.availability_level = 0;
        h.reason = "Models not loaded";
    } else if (h.pending_requests > h.queue_capacity / 2) {
        h.status = "degraded";
        h.availability_level = 1;
        h.reason = "High load";
    } else {
        h.status = "ok";
        h.availability_level = 2;
        h.reason = "";
    }

    return h;
}

ServerStats OcrServer::stats() const {
    ServerStats s;
    s.total_requests = impl_->total_requests.load();
    s.successful_requests = impl_->successful_requests.load();
    s.failed_requests = impl_->failed_requests.load();
    s.active_workers = impl_->pool ? impl_->pool->active_workers.load() : 0;
    s.pool_size = impl_->pool ? static_cast<int>(impl_->pool->workers.size()) : 0;
    s.queue_capacity = impl_->config.queue_capacity;
    s.pending_requests = impl_->pool ? impl_->pool->pending_tasks.load() : 0;
    s.models_loaded = impl_->pool && !impl_->pool->workers.empty() &&
                      impl_->pool->workers[0]->is_loaded();
    s.start_time = impl_->start_time;
    return s;
}

} // namespace shmtu::cas_ocr

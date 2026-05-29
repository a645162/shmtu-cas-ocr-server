// SHMTU CAS OCR Server — RESTful API + Legacy TCP
// Refactored: C++20, PIMPL, worker pool, graceful shutdown via jthread/stop_token

#include <shmtu/cas_ocr/server.h>
#include <shmtu/cas_ocr/cas_ocr.h>
#include <shmtu/cas_ocr/version.h>

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <expected>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

static shmtu::cas::ocr::OcrServer* g_server = nullptr;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::printf("\nReceived signal %d. Shutting down gracefully...\n", sig);
        if (g_server) {
            g_server->stop();
        }
    }
}

static void print_banner() {
    std::printf("ShangHai Maritime University\n");
    std::printf("  SHMTU CAS OCR Server V%s\n", SHMTU_CAS_OCR_SERVER_VERSION);
    std::printf("  Author: Haomin Kong\n");
    std::printf("  C++23 | RESTful API + TCP\n");
    std::printf("\n");
}

template <typename Int>
static std::expected<Int, std::string> parse_integer_arg(std::string_view value, std::string_view name) {
    Int parsed{};
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        return std::unexpected("Invalid value for " + std::string(name) + ": " + std::string(value));
    }
    return parsed;
}

static std::expected<bool, std::string> parse_bool_value(std::string_view value, std::string_view name) {
    if (value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON" ||
        value == "yes" || value == "YES") {
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" || value == "off" || value == "OFF" ||
        value == "no" || value == "NO") {
        return false;
    }
    return std::unexpected("Invalid value for " + std::string(name) + ": " + std::string(value));
}

static std::expected<void, std::string> apply_env_overrides(shmtu::cas::ocr::ServerConfig& config) {
    auto read_int = [&](const char* key, int& target, std::string_view name) -> std::expected<void, std::string> {
        if (const char* value = std::getenv(key)) {
            auto parsed = parse_integer_arg<int>(value, name);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            target = *parsed;
        }
        return {};
    };

    if (const char* value = std::getenv("SHMTU_HTTP_HOST")) {
        config.http_host = value;
    }
    if (const char* value = std::getenv("SHMTU_TCP_HOST")) {
        config.tcp_host = value;
    }
    if (const char* value = std::getenv("SHMTU_MODEL_DIR")) {
        config.model_dir = value;
    }
    if (const char* value = std::getenv("SHMTU_PRECISION")) {
        config.precision = value;
    }
    if (const char* value = std::getenv("SHMTU_SERVER_NAME")) {
        config.server_name = value;
    }
    if (const char* value = std::getenv("SHMTU_USE_GPU")) {
        auto parsed = parse_bool_value(value, "SHMTU_USE_GPU");
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        config.use_gpu = *parsed;
    }

    if (auto result = read_int("SHMTU_HTTP_PORT", config.http_port, "SHMTU_HTTP_PORT"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_TCP_PORT", config.tcp_port, "SHMTU_TCP_PORT"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_WORKERS", config.worker_count, "SHMTU_WORKERS"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_QUEUE_CAPACITY", config.queue_capacity, "SHMTU_QUEUE_CAPACITY"); !result) {
        return result;
    }
    if (auto result = read_int("SHMTU_NCNN_THREADS", config.inference_threads, "SHMTU_NCNN_THREADS"); !result) {
        return result;
    }

    return {};
}

static void resolve_auto_tuning(shmtu::cas::ocr::ServerConfig& config) {
    const auto hardware_threads = std::max(1u, std::thread::hardware_concurrency());

    if (config.inference_threads <= 0) {
        config.inference_threads = config.use_gpu
            ? 1
            : static_cast<int>(std::min(hardware_threads, 4u));
    }

    if (config.worker_count <= 0) {
        if (config.use_gpu) {
            config.worker_count = static_cast<int>(std::min(hardware_threads, 2u));
        } else {
            const auto auto_workers = std::max(
                1u,
                std::min(hardware_threads / static_cast<unsigned>(std::max(1, config.inference_threads)), 8u));
            config.worker_count = static_cast<int>(auto_workers);
        }
    }

    if (config.queue_capacity <= 0) {
        config.queue_capacity = std::max(16, config.worker_count * 4);
    }
}

static std::expected<shmtu::cas::ocr::ServerConfig, std::string> parse_args(int argc, char* argv[]) {
    shmtu::cas::ocr::ServerConfig config;
    if (auto env_result = apply_env_overrides(config); !env_result) {
        return std::unexpected(env_result.error());
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            std::printf("Usage: shmtu-cas-ocr-server [OPTIONS]\n\n");
            std::printf("Options:\n");
            std::printf("  --http-port <port>      HTTP port (default: %d)\n", config.http_port);
            std::printf("  --tcp-port <port>       TCP port (default: %d)\n", config.tcp_port);
            std::printf("  --model-dir <path>      Model directory (default: %s)\n", config.model_dir.c_str());
            std::printf("  --precision <fp16|fp32> Model precision (default: %s)\n", config.precision.c_str());
            std::printf("  --workers <n>           Number of OCR workers (0 = auto, default: %d)\n", config.worker_count);
            std::printf("  --ncnn-threads <n>      NCNN CPU threads per worker (0 = auto, default: %d)\n",
                        config.inference_threads);
            std::printf("  --use-gpu               Enable GPU acceleration\n");
            std::printf("  --queue-capacity <n>    Max pending requests (0 = auto, default: %d)\n",
                        config.queue_capacity);
            std::printf("\nEnvironment:\n");
            std::printf("  SHMTU_HTTP_HOST SHMTU_HTTP_PORT SHMTU_TCP_HOST SHMTU_TCP_PORT\n");
            std::printf("  SHMTU_MODEL_DIR SHMTU_PRECISION SHMTU_USE_GPU SHMTU_SERVER_NAME\n");
            std::printf("  SHMTU_WORKERS SHMTU_QUEUE_CAPACITY SHMTU_NCNN_THREADS\n");
            std::printf("  --help, -h              Show this help\n");
            std::exit(0);
        } else if (arg == "--http-port" && i + 1 < argc) {
            auto parsed = parse_integer_arg<int>(argv[i + 1], "http-port");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.http_port = *parsed;
            ++i;
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            auto parsed = parse_integer_arg<int>(argv[i + 1], "tcp-port");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.tcp_port = *parsed;
            ++i;
        } else if (arg == "--model-dir" && i + 1 < argc) {
            config.model_dir = argv[++i];
        } else if (arg == "--precision" && i + 1 < argc) {
            config.precision = argv[++i];
        } else if (arg == "--workers" && i + 1 < argc) {
            auto parsed = parse_integer_arg<int>(argv[i + 1], "workers");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.worker_count = *parsed;
            ++i;
        } else if (arg == "--ncnn-threads" && i + 1 < argc) {
            auto parsed = parse_integer_arg<int>(argv[i + 1], "ncnn-threads");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.inference_threads = *parsed;
            ++i;
        } else if (arg == "--use-gpu") {
            config.use_gpu = true;
        } else if (arg == "--queue-capacity" && i + 1 < argc) {
            auto parsed = parse_integer_arg<int>(argv[i + 1], "queue-capacity");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.queue_capacity = *parsed;
            ++i;
        } else {
            return std::unexpected("Unknown argument: " + arg);
        }
    }

    if (config.precision != "fp16" && config.precision != "fp32") {
        return std::unexpected("Unsupported precision: " + config.precision);
    }
    if (config.http_port <= 0 || config.tcp_port <= 0) {
        return std::unexpected("Ports must be positive integers");
    }
    if (config.worker_count < 0) {
        return std::unexpected("Worker count must be zero or positive");
    }
    if (config.queue_capacity < 0) {
        return std::unexpected("Queue capacity must be zero or positive");
    }
    if (config.inference_threads < 0) {
        return std::unexpected("NCNN threads must be zero or positive");
    }

    resolve_auto_tuning(config);
    return config;
}

int main(int argc, char* argv[]) {
    print_banner();

    auto config = parse_args(argc, argv);
    if (!config) {
        std::fprintf(stderr, "Error: %s\nUse --help for usage.\n", config.error().c_str());
        return 1;
    }

    std::printf("Configuration:\n");
    std::printf("  HTTP port:      %d\n", config->http_port);
    std::printf("  TCP port:       %d\n", config->tcp_port);
    std::printf("  Model dir:      %s\n", config->model_dir.c_str());
    std::printf("  Precision:      %s\n", config->precision.c_str());
    std::printf("  Workers:        %d\n", config->worker_count);
    std::printf("  NCNN threads:   %d\n", config->inference_threads);
    std::printf("  Use GPU:        %s\n", config->use_gpu ? "true" : "false");
    std::printf("  Queue capacity: %d\n", config->queue_capacity);

#ifdef NCNN_SUPPORT_VULKAN
    if (config->use_gpu) {
        int gpu_count = shmtu::cas::ocr::CasOcr::gpu_count();
        if (gpu_count == 0) {
            std::fprintf(stderr, "WARNING: GPU requested but no Vulkan devices found. Falling back to CPU.\n");
            config->use_gpu = false;
        } else {
            std::printf("\nGPU Devices (%d):\n", gpu_count);
            auto gpus = shmtu::cas::ocr::CasOcr::all_gpu_info();
            for (const auto& gpu : gpus) {
                std::printf("  [%d] %s (API %u, %u MB)\n",
                            gpu.device_index, gpu.device_name.c_str(),
                            gpu.api_version, gpu.device_memory);
            }
        }
    }
#endif

    std::printf("\n");

    shmtu::cas::ocr::OcrServer server(*config);
    g_server = &server;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int ret = server.run();

    g_server = nullptr;
    return ret;
}
